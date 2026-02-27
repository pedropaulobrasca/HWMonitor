"""
HW Monitor v2 — Script de Host (Python)
Coleta dados de hardware e envia via Serial para o ESP32.
Usa LibreHardwareMonitorLib para sensores no Windows.
Lê FPS via RTSS (RivaTuner Statistics Server) shared memory.
"""

import sys
import os
import json
import time
import ctypes
import ctypes.wintypes
import logging

import psutil
import serial
import serial.tools.list_ports

# ── LibreHardwareMonitor via pythonnet ───────────────────────
def _find_lhm_dll() -> str:
    """Procura a DLL do LHM: primeiro junto ao exe, depois na instalação winget."""
    # Se empacotado com PyInstaller, DLL pode estar junto ao exe
    if getattr(sys, 'frozen', False):
        bundled = os.path.join(sys._MEIPASS, "LibreHardwareMonitorLib.dll")
        if os.path.exists(bundled):
            return bundled

    # Instalação winget padrão
    return os.path.join(
        os.environ.get("LOCALAPPDATA", ""),
        r"Microsoft\WinGet\Packages\LibreHardwareMonitor.LibreHardwareMonitor_Microsoft.Winget.Source_8wekyb3d8bbwe",
        "LibreHardwareMonitorLib.dll",
    )

LHM_DLL = _find_lhm_dll()

HAS_LHM = False
lhm_computer = None

try:
    import clr
    clr.AddReference(LHM_DLL)
    from LibreHardwareMonitor.Hardware import Computer, HardwareType, SensorType
    HAS_LHM = True
except Exception:
    pass

# ── Configuração ─────────────────────────────────────────────
BAUD_RATE     = 115200
SEND_INTERVAL = 1.0
LOG_LEVEL     = logging.INFO

# ── Logger ───────────────────────────────────────────────────
logging.basicConfig(
    level=LOG_LEVEL,
    format="[%(asctime)s] %(levelname)s: %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("HWMonitor")


# =============================================================
# RTSS Shared Memory — Leitura de FPS
# =============================================================
RTSS_SHARED_MEMORY_NAME = "RTSSSharedMemoryV2"

# ── Configurar kernel32 com tipos 64-bit corretos ───────────
_kernel32 = ctypes.windll.kernel32
_kernel32.OpenFileMappingW.restype = ctypes.wintypes.HANDLE
_kernel32.MapViewOfFile.restype = ctypes.c_void_p
_kernel32.UnmapViewOfFile.argtypes = [ctypes.c_void_p]
_kernel32.UnmapViewOfFile.restype = ctypes.wintypes.BOOL
_kernel32.CloseHandle.argtypes = [ctypes.wintypes.HANDLE]

# Handle para o file mapping do RTSS — manter aberto para performance
_rtss_handle = None
_rtss_map_view = None


def _open_rtss_shared_memory():
    """Abre o shared memory do RTSS. Retorna (handle, map_view) ou (None, None)."""
    global _rtss_handle, _rtss_map_view

    if _rtss_handle is not None:
        return _rtss_handle, _rtss_map_view

    try:
        handle = _kernel32.OpenFileMappingW(0x0004, False, RTSS_SHARED_MEMORY_NAME)
        if not handle:
            return None, None

        map_view = _kernel32.MapViewOfFile(handle, 0x0004, 0, 0, 0)
        if not map_view:
            _kernel32.CloseHandle(handle)
            return None, None

        _rtss_handle = handle
        _rtss_map_view = map_view
        return handle, map_view
    except Exception:
        return None, None


def close_rtss_shared_memory():
    """Fecha o shared memory do RTSS."""
    global _rtss_handle, _rtss_map_view
    if _rtss_map_view:
        _kernel32.UnmapViewOfFile(_rtss_map_view)
        _rtss_map_view = None
    if _rtss_handle:
        _kernel32.CloseHandle(_rtss_handle)
        _rtss_handle = None


def read_rtss_fps() -> int:
    """Lê FPS do RTSS shared memory. Retorna 0 se RTSS não estiver rodando."""
    handle, map_view = _open_rtss_shared_memory()
    if not map_view:
        return 0

    try:
        mv = map_view
        sig = ctypes.c_uint32.from_address(mv).value
        if sig != 0x52545353:
            return 0

        app_entry_size = ctypes.c_uint32.from_address(mv + 8).value
        app_arr_offset = ctypes.c_uint32.from_address(mv + 12).value
        app_arr_size   = ctypes.c_uint32.from_address(mv + 16).value  # num entries

        if app_arr_size == 0 or app_entry_size == 0:
            return 0

        best_fps = 0
        for i in range(app_arr_size):
            base = mv + app_arr_offset + i * app_entry_size
            pid = ctypes.c_uint32.from_address(base).value
            if pid == 0:
                continue

            # Offsets dentro do app entry: +264=flags, +268=time0, +272=time1, +276=frames
            time0  = ctypes.c_uint32.from_address(base + 268).value
            time1  = ctypes.c_uint32.from_address(base + 272).value
            frames = ctypes.c_uint32.from_address(base + 276).value

            dt = time1 - time0
            if dt > 0 and frames > 0:
                fps = int(round(frames * 1000.0 / dt))
                if fps > best_fps:
                    best_fps = fps

        return best_fps
    except Exception:
        close_rtss_shared_memory()
        return 0


# =============================================================
# LibreHardwareMonitor — init / leitura
# =============================================================
def init_lhm():
    """Inicializa o LibreHardwareMonitor."""
    global lhm_computer
    if not HAS_LHM:
        log.warning("LibreHardwareMonitorLib não disponível.")
        return

    try:
        lhm_computer = Computer()
        lhm_computer.IsCpuEnabled = True
        lhm_computer.IsGpuEnabled = True
        lhm_computer.Open()
        log.info("LibreHardwareMonitor inicializado.")
    except Exception as e:
        log.warning(f"Falha ao iniciar LHM: {e}")
        lhm_computer = None


def read_lhm_sensors() -> dict:
    """Lê sensores via LibreHardwareMonitor."""
    result = {
        "gpu_load": 0,
        "gpu_temp": 0,
        "cpu_temp": 0,
        "cpu_clk": 0,
        "gpu_clk": 0,
    }
    if not lhm_computer:
        return result

    try:
        for hw in lhm_computer.Hardware:
            hw.Update()

            # GPU
            if hw.HardwareType in (HardwareType.GpuAmd, HardwareType.GpuNvidia, HardwareType.GpuIntel):
                for sensor in hw.Sensors:
                    if sensor.SensorType == SensorType.Temperature and sensor.Name == "GPU Core":
                        if sensor.Value is not None:
                            result["gpu_temp"] = int(sensor.Value)
                    if sensor.SensorType == SensorType.Load and sensor.Name == "GPU Core":
                        if sensor.Value is not None:
                            result["gpu_load"] = int(sensor.Value)
                    if sensor.SensorType == SensorType.Clock and sensor.Name == "GPU Core":
                        if sensor.Value is not None:
                            result["gpu_clk"] = int(sensor.Value)

            # CPU
            if hw.HardwareType == HardwareType.Cpu:
                for sensor in hw.Sensors:
                    if sensor.SensorType == SensorType.Temperature and sensor.Name == "CPU Package":
                        if sensor.Value is not None:
                            result["cpu_temp"] = int(sensor.Value)
                    if sensor.SensorType == SensorType.Clock and "Core" in sensor.Name:
                        if sensor.Value is not None:
                            clk = int(sensor.Value)
                            if clk > result["cpu_clk"]:
                                result["cpu_clk"] = clk

                # Fallback: Core Max se Package não disponível
                if result["cpu_temp"] == 0:
                    for sensor in hw.Sensors:
                        if sensor.SensorType == SensorType.Temperature and "Core" in sensor.Name:
                            if sensor.Value is not None:
                                result["cpu_temp"] = int(sensor.Value)
                                break
    except Exception as e:
        log.debug(f"Erro ao ler LHM: {e}")

    return result


# =============================================================
# Detecção automática da porta COM do ESP32
# =============================================================
def find_esp32_port() -> str | None:
    esp_ids = [
        (0x303A, 0x1001),
        (0x303A, 0x80FF),
        (0x1A86, 0x55D4),
        (0x10C4, 0xEA60),
    ]

    ports = serial.tools.list_ports.comports()
    for port in ports:
        for vid, pid in esp_ids:
            if port.vid == vid and port.pid == pid:
                log.info(f"ESP32 encontrado: {port.device} ({port.description})")
                return port.device

    for port in ports:
        desc = (port.description or "").lower()
        if "esp32" in desc or "cp210" in desc or "ch910" in desc:
            log.info(f"ESP32 (por descrição): {port.device} ({port.description})")
            return port.device

    return None


# =============================================================
# Coleta de dados
# =============================================================
def collect_data() -> dict:
    lhm = read_lhm_sensors()
    fps = read_rtss_fps()
    return {
        "cpu":      int(psutil.cpu_percent(interval=None)),
        "gpu":      lhm["gpu_load"],
        "ram":      int(psutil.virtual_memory().percent),
        "cpu_temp": lhm["cpu_temp"],
        "gpu_temp": lhm["gpu_temp"],
        "fps":      fps,
        "cpu_clk":  lhm["cpu_clk"],
        "gpu_clk":  lhm["gpu_clk"],
        "time":     time.strftime("%H:%M"),
        "date":     time.strftime("%d %b"),
    }


# =============================================================
# Baixa prioridade
# =============================================================
def set_low_priority():
    try:
        p = psutil.Process(os.getpid())
        if sys.platform == "win32":
            p.nice(psutil.BELOW_NORMAL_PRIORITY_CLASS)
        else:
            p.nice(10)
        log.info("Prioridade do processo reduzida.")
    except Exception as e:
        log.warning(f"Não foi possível reduzir prioridade: {e}")


# =============================================================
# Loop principal
# =============================================================
def main():
    set_low_priority()

    # Inicializa LHM
    init_lhm()

    # Inicializa medição de CPU (primeiro valor é sempre 0)
    psutil.cpu_percent(interval=None)

    # Detecta porta COM
    log.info("Procurando ESP32...")
    port = find_esp32_port()

    if not port:
        log.error("ESP32 não encontrado! Portas disponíveis:")
        for p in serial.tools.list_ports.comports():
            log.error(f"  {p.device}: {p.description} (VID={p.vid} PID={p.pid})")
        log.error("Conecte o ESP32 e tente novamente.")
        sys.exit(1)

    # Abre conexão serial
    try:
        ser = serial.Serial()
        ser.port = port
        ser.baudrate = BAUD_RATE
        ser.timeout = 1
        ser.dtr = False
        ser.rts = False
        ser.open()
        log.info(f"Conectado em {port} @ {BAUD_RATE} baud")
    except serial.SerialException as e:
        log.error(f"Erro ao abrir {port}: {e}")
        sys.exit(1)

    # Aguarda ESP32 inicializar
    time.sleep(2)

    log.info("Enviando dados... (Ctrl+C para parar)")
    log.info("FPS via RTSS: %s", "disponível" if read_rtss_fps() >= 0 else "não detectado")

    try:
        while True:
            data = collect_data()
            payload = json.dumps(data) + "\n"

            try:
                ser.write(payload.encode("utf-8"))
                log.debug(f"Enviado: {data}")
            except serial.SerialException:
                log.warning("Conexão perdida. Reconectando...")
                ser.close()
                time.sleep(2)

                port = find_esp32_port()
                if port:
                    try:
                        ser = serial.Serial()
                        ser.port = port
                        ser.baudrate = BAUD_RATE
                        ser.timeout = 1
                        ser.dtr = False
                        ser.rts = False
                        ser.open()
                        log.info(f"Reconectado em {port}")
                        time.sleep(1)
                    except serial.SerialException:
                        log.error("Falha na reconexão.")
                else:
                    log.error("ESP32 não encontrado para reconexão.")

            time.sleep(SEND_INTERVAL)

    except KeyboardInterrupt:
        log.info("Encerrado pelo usuário.")
    finally:
        close_rtss_shared_memory()
        if lhm_computer:
            lhm_computer.Close()
        if ser and ser.is_open:
            ser.close()
            log.info("Porta serial fechada.")


if __name__ == "__main__":
    main()

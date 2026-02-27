"""
Flash helper — monitora porta COM e flasha quando ESP32 aparecer.
Desconecte o USB, segure BOOT, reconecte, e solte BOOT.
"""
import time
import subprocess
import sys
import serial.tools.list_ports

ESP_VIDS = [0x303A, 0x1A86, 0x10C4]
FIRMWARE_DIR = r"C:\Users\Peter\Documents\HWMonitor\firmware"

def find_esp_port():
    for p in serial.tools.list_ports.comports():
        if p.vid in ESP_VIDS:
            return p.device
    return None

print("=" * 50)
print("  FLASH HELPER - T-Display-S3")
print("=" * 50)
print()
print("1. DESCONECTE o cabo USB agora")
print("   (esperando o dispositivo sumir...)")

# Espera o dispositivo desaparecer
while find_esp_port():
    time.sleep(0.3)

print()
print("2. USB desconectado! Agora:")
print("   - Segure o botao BOOT (lateral do case)")
print("   - Conecte o cabo USB")
print("   - Espere 2 segundos")
print("   - Solte o BOOT")
print()
print("   (esperando dispositivo aparecer...)")

# Espera o dispositivo aparecer
port = None
for i in range(60):  # max 30 segundos
    port = find_esp_port()
    if port:
        break
    time.sleep(0.5)

if not port:
    print("TIMEOUT - Nenhum dispositivo encontrado!")
    sys.exit(1)

print(f"\n>>> Dispositivo encontrado em {port}!")
print(">>> Iniciando flash em 1 segundo...")
time.sleep(1)

# Roda o PlatformIO para flashar
result = subprocess.run(
    [sys.executable, "-m", "platformio", "run", "-t", "upload",
     "--upload-port", port],
    cwd=FIRMWARE_DIR
)

if result.returncode == 0:
    print("\n>>> FLASH CONCLUIDO COM SUCESSO! <<<")
else:
    print(f"\n>>> Flash falhou (code {result.returncode})")
    print(">>> Tente novamente sem segurar BOOT ao reconectar")

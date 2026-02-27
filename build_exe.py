"""
Build script — gera HWMonitor.exe via PyInstaller.
Uso: python build_exe.py
"""
import subprocess
import sys
import os
import shutil

HOST_DIR = os.path.join(os.path.dirname(__file__), "host")
DIST_DIR = os.path.join(os.path.dirname(__file__), "dist")

LHM_DIR = os.path.join(
    os.environ.get("LOCALAPPDATA", ""),
    r"Microsoft\WinGet\Packages\LibreHardwareMonitor.LibreHardwareMonitor_Microsoft.Winget.Source_8wekyb3d8bbwe",
)

# DLLs do LHM que precisam ser incluídas
LHM_DLLS = [
    "LibreHardwareMonitorLib.dll",
    "HidSharp.dll",
]

# Montar lista de --add-data para as DLLs do LHM
add_data_args = []
for dll in LHM_DLLS:
    dll_path = os.path.join(LHM_DIR, dll)
    if os.path.exists(dll_path):
        add_data_args.extend(["--add-data", f"{dll_path};."])
        print(f"  + {dll}")
    else:
        print(f"  ! {dll} não encontrado, pulando")

print("\nGerando HWMonitor.exe...")

cmd = [
    sys.executable, "-m", "PyInstaller",
    "--onefile",
    "--name", "HWMonitor",
    "--distpath", DIST_DIR,
    "--workpath", os.path.join(os.path.dirname(__file__), "build"),
    "--specpath", os.path.join(os.path.dirname(__file__), "build"),
    "--clean",
    # Hidden imports que PyInstaller não detecta sozinho
    "--hidden-import", "clr",
    "--hidden-import", "pythonnet",
    "--hidden-import", "serial.tools.list_ports",
    "--hidden-import", "serial.tools.list_ports_windows",
    *add_data_args,
    os.path.join(HOST_DIR, "monitor.py"),
]

result = subprocess.run(cmd)

if result.returncode == 0:
    exe_path = os.path.join(DIST_DIR, "HWMonitor.exe")
    print(f"\nSUCESSO! Executável em: {exe_path}")

    # Copiar pro Desktop
    desktop = os.path.join(os.path.expanduser("~"), "Desktop")
    if os.path.isdir(desktop):
        dest = os.path.join(desktop, "HWMonitor.exe")
        shutil.copy2(exe_path, dest)
        print(f"Copiado para: {dest}")
else:
    print(f"\nFALHOU (code {result.returncode})")
    sys.exit(1)

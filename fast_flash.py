"""
Fast Flash — detecta porta e flasha INSTANTANEAMENTE com esptool.
"""
import time
import subprocess
import sys
import serial.tools.list_ports

BUILD = r"C:\Users\Peter\Documents\HWMonitor\firmware\.pio\build\lilygo-t-display-s3"
BOOT_APP = r"C:\Users\Peter\.platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin"

def find_esp():
    for p in serial.tools.list_ports.comports():
        if p.vid == 0x303A:
            return p.device
    return None

print("1. DESCONECTE o cabo USB agora")
while find_esp():
    time.sleep(0.2)

print("2. RECONECTE o cabo USB")
port = None
for _ in range(120):
    port = find_esp()
    if port:
        break
    time.sleep(0.25)

if not port:
    print("TIMEOUT!")
    sys.exit(1)

print(f">>> {port} detectado! Flashando...")

# Roda esptool DIRETO — sem delay
subprocess.run([
    sys.executable, "-m", "esptool",
    "--chip", "esp32s3",
    "--port", port,
    "--baud", "921600",
    "--before", "default-reset",
    "--after", "hard-reset",
    "write-flash", "-z",
    "--flash-mode", "dio",
    "--flash-freq", "80m",
    "--flash-size", "16MB",
    "0x0000", f"{BUILD}\\bootloader.bin",
    "0x8000", f"{BUILD}\\partitions.bin",
    "0xe000", BOOT_APP,
    "0x10000", f"{BUILD}\\firmware.bin",
])

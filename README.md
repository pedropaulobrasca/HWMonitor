# HW Monitor

Monitor de hardware em tempo real usando um **Lilygo T-Display-S3** (ESP32-S3 + display 1.9" 320x170).

O PC coleta dados de CPU, GPU, RAM, temperaturas, clocks e FPS e envia via serial USB para o display.

## Telas

O botao direito do T-Display-S3 (GPIO 14) alterna entre 3 telas:

### Tela 1 — Dashboard
CPU, GPU e RAM com barras de progresso + FPS.

### Tela 2 — Temps & Info
Temperaturas de CPU e GPU com barras + clocks em MHz + modelo do hardware.

### Tela 3 — Gaming
FPS gigante centralizado + temperaturas de CPU e GPU nos cantos.

## Hardware necessario

- **Lilygo T-Display-S3** (ESP32-S3, display ST7789 170x320)
- Cabo USB-C

## Software necessario

### No PC (Windows)

| Software | Para que serve | Como instalar |
|----------|----------------|---------------|
| **Python 3.10+** | Rodar o script host | [python.org](https://www.python.org/downloads/) |
| **PlatformIO** | Compilar e flashar o firmware | `pip install platformio` |
| **LibreHardwareMonitor** | Ler temperaturas, clocks, uso de GPU | `winget install LibreHardwareMonitor` |
| **MSI Afterburner + RTSS** | Ler FPS dos jogos | [msi.com/afterburner](https://www.msi.com/Landing/afterburner/graphics-cards) |

> **Nota sobre RTSS**: Durante a instalacao do MSI Afterburner, marque a opcao para instalar o **RivaTuner Statistics Server (RTSS)**. O FPS so aparece quando o RTSS esta rodando e detectando um jogo.

### Dependencias Python

```bash
cd host
pip install -r requirements.txt
```

Tambem precisa do `pythonnet` para acessar o LibreHardwareMonitor:
```bash
pip install pythonnet
```

## Setup

### 1. Compilar o firmware

```bash
cd firmware
pio run
```

### 2. Flashar no T-Display-S3

Opcao rapida (recomendada):
```bash
python fast_flash.py
```
Vai pedir pra desconectar e reconectar o USB.

Opcao alternativa (com botao BOOT):
```bash
python flash_helper.py
```

### 3. Rodar o monitor

```bash
cd host
python monitor.py
```

O script detecta o ESP32 automaticamente, conecta na porta serial e comeca a enviar dados.

## Estrutura do projeto

```
HWMonitor/
  firmware/
    src/main.cpp          # Firmware do ESP32 (display + botao)
    platformio.ini        # Config do PlatformIO
  host/
    monitor.py            # Script Python que coleta e envia dados
    requirements.txt      # Dependencias Python
  fast_flash.py           # Flash rapido (desconecta/reconecta USB)
  flash_helper.py         # Flash com botao BOOT
```

## Dados coletados

| Dado | Fonte | Notas |
|------|-------|-------|
| CPU % | psutil | |
| GPU % | LibreHardwareMonitor | |
| RAM % | psutil | |
| CPU Temp | LibreHardwareMonitor | Sensor "CPU Package" |
| GPU Temp | LibreHardwareMonitor | Sensor "GPU Core" |
| CPU Clock | LibreHardwareMonitor | Clock maximo entre cores |
| GPU Clock | LibreHardwareMonitor | Sensor "GPU Core" clock |
| FPS | RTSS shared memory | Precisa do MSI Afterburner + RTSS rodando |
| Horario | Relogio do PC | Formato HH:MM |

## Troubleshooting

### FPS mostra "---"
- Verifique se o **RTSS** esta rodando (icone azul na bandeja do sistema)
- No RTSS, verifique se **Application detection level** esta em "High"
- Alguns jogos com anti-cheat bloqueiam o RTSS

### Temperaturas mostram 0
- O **LibreHardwareMonitor** precisa estar instalado via winget
- Rode o `monitor.py` como **Administrador** para acesso completo aos sensores

### ESP32 nao encontrado
- Verifique se o cabo USB e de dados (nao so carga)
- Instale o driver USB se necessario (o T-Display-S3 usa USB nativo do ESP32-S3)

### Display mostra "PC OFFLINE"
- O `monitor.py` precisa estar rodando no PC
- Verifique se a conexao serial esta funcionando (o script mostra a porta COM no log)

// ============================================================
// HW Monitor v2 — Firmware para Lilygo T-Display-S3
// ESP32-S3 + Display ST7789 1.9" (170x320)
// Comunicação Serial USB 115200 baud
// Bibliotecas: TFT_eSPI, ArduinoJson
// Multi-tela com botão GPIO 14
// ============================================================

#include <TFT_eSPI.h>
#include <ArduinoJson.h>

// ── Protótipos de funções ───────────────────────────────────
void readSerial();
void parseJson(const String &json);
void drawOfflineScreen();
void drawScreen1();
void drawScreen2();
void drawScreen3();
void drawHeader(const char* title);
void drawMetric(const char* label, int value, int x, int y,
                int w, int h, uint16_t color, const char* unit);
void drawScanlineEffect();
void drawPageIndicator();
uint16_t lightenColor(uint16_t color);
void IRAM_ATTR onButtonPress();

// ── Configuração do Display ─────────────────────────────────
TFT_eSPI    tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);  // Double buffering

// ── Dimensões da tela (landscape) ───────────────────────────
static const int SCREEN_W = 320;
static const int SCREEN_H = 170;

// ── Botão ───────────────────────────────────────────────────
static const int BTN_PIN = 14;
volatile bool btnPressed = false;
unsigned long lastBtnTime = 0;
static const unsigned long DEBOUNCE_MS = 250;
int currentScreen = 0;
static const int NUM_SCREENS = 3;

// ── Paleta de cores (RGB565) ────────────────────────────────
static const uint16_t COL_BG        = TFT_BLACK;
static const uint16_t COL_CYAN      = 0x07FF;  // CPU
static const uint16_t COL_MAGENTA   = 0xF81F;  // GPU
static const uint16_t COL_GREEN     = 0x07E0;  // RAM
static const uint16_t COL_ORANGE    = 0xFDA0;  // TEMP
static const uint16_t COL_YELLOW    = 0xFFE0;  // FPS
static const uint16_t COL_BAR_BG    = 0x2104;  // cinza escuro
static const uint16_t COL_TEXT      = 0xFFFF;
static const uint16_t COL_DIM       = 0x7BEF;  // cinza médio
static const uint16_t COL_RED       = 0xF800;
static const uint16_t COL_SCANLINE  = 0x0821;  // verde escuro sutil

// ── Dados recebidos ─────────────────────────────────────────
struct HWData {
  int cpu      = 0;
  int gpu      = 0;
  int ram      = 0;
  int cpu_temp = 0;
  int gpu_temp = 0;
  int fps      = 0;
  int cpu_clk  = 0;
  int gpu_clk  = 0;
  char hora[6] = "--:--";
};

HWData hw;

// ── Controle de timeout ─────────────────────────────────────
unsigned long lastDataTime   = 0;
static const unsigned long TIMEOUT_MS = 5000;
bool isOffline               = true;
bool wasOffline              = true;  // força primeiro desenho

// ── Buffer serial ───────────────────────────────────────────
String serialBuffer = "";

// ── Efeito scanline (ativado quando temp > 80) ──────────────
bool scanlineActive = false;
int  scanlineOffset = 0;

// ── ISR do botão ────────────────────────────────────────────
void IRAM_ATTR onButtonPress() {
  btnPressed = true;
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  // T-Display-S3: habilitar energia do display (GPIO 15)
  pinMode(15, OUTPUT);
  digitalWrite(15, HIGH);

  tft.init();
  tft.setRotation(1);  // landscape

  // Backlight ON (GPIO 38)
  pinMode(38, OUTPUT);
  digitalWrite(38, HIGH);

  // Botão direito (GPIO 14) com pull-up interno
  pinMode(BTN_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), onButtonPress, FALLING);

  tft.fillScreen(COL_BG);

  // Criar sprite do tamanho total da tela (double buffer)
  spr.createSprite(SCREEN_W, SCREEN_H);
  spr.setTextDatum(TL_DATUM);

  lastDataTime = millis();
  drawOfflineScreen();
}

// ============================================================
// LOOP PRINCIPAL
// ============================================================
void loop() {
  // Lê dados da serial
  readSerial();

  // Verifica botão com debounce
  if (btnPressed) {
    unsigned long now = millis();
    if (now - lastBtnTime > DEBOUNCE_MS) {
      currentScreen = (currentScreen + 1) % NUM_SCREENS;
      lastBtnTime = now;
    }
    btnPressed = false;
  }

  // Verifica timeout
  bool currentlyOffline = (millis() - lastDataTime > TIMEOUT_MS);

  if (currentlyOffline && !wasOffline) {
    isOffline = true;
    drawOfflineScreen();
    wasOffline = true;
  } else if (!currentlyOffline && isOffline) {
    isOffline  = false;
    wasOffline = false;
  }

  if (!isOffline) {
    if (currentScreen == 0) {
      drawScreen1();
    } else if (currentScreen == 1) {
      drawScreen2();
    } else {
      drawScreen3();
    }
    wasOffline = false;
  }

  delay(50);  // ~20 FPS
}

// ============================================================
// LEITURA SERIAL + PARSE JSON
// ============================================================
void readSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() > 0) {
        parseJson(serialBuffer);
        serialBuffer = "";
      }
    } else {
      serialBuffer += c;
      // Proteção contra overflow
      if (serialBuffer.length() > 512) {
        serialBuffer = "";
      }
    }
  }
}

void parseJson(const String &json) {
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, json);

  if (err) return;  // ignora JSON inválido

  hw.cpu      = constrain(doc["cpu"]      | 0, 0, 100);
  hw.gpu      = constrain(doc["gpu"]      | 0, 0, 100);
  hw.ram      = constrain(doc["ram"]      | 0, 0, 100);
  hw.cpu_temp = constrain(doc["cpu_temp"] | 0, 0, 120);
  hw.gpu_temp = constrain(doc["gpu_temp"] | 0, 0, 120);
  hw.fps      = constrain(doc["fps"]      | 0, 0, 9999);
  hw.cpu_clk  = constrain(doc["cpu_clk"]  | 0, 0, 9999);
  hw.gpu_clk  = constrain(doc["gpu_clk"]  | 0, 0, 9999);

  const char* t = doc["time"] | "--:--";
  strncpy(hw.hora, t, sizeof(hw.hora) - 1);
  hw.hora[sizeof(hw.hora) - 1] = '\0';

  lastDataTime = millis();
  isOffline    = false;
}

// ============================================================
// TELA OFFLINE
// ============================================================
void drawOfflineScreen() {
  spr.fillSprite(COL_BG);

  // Ícone de desconexão (X grande)
  int cx = SCREEN_W / 2;
  int cy = SCREEN_H / 2 - 15;
  for (int i = -2; i <= 2; i++) {
    spr.drawLine(cx - 20 + i, cy - 20, cx + 20 + i, cy + 20, COL_RED);
    spr.drawLine(cx + 20 + i, cy - 20, cx - 20 + i, cy + 20, COL_RED);
  }

  // Círculo ao redor
  spr.drawCircle(cx, cy, 30, COL_DIM);

  // Texto
  spr.setTextColor(COL_RED);
  spr.setTextSize(2);
  spr.setTextDatum(MC_DATUM);
  spr.drawString("PC OFFLINE", cx, cy + 50);

  spr.setTextColor(COL_DIM);
  spr.setTextSize(1);
  spr.drawString("Aguardando conexao serial...", cx, cy + 70);

  spr.setTextDatum(TL_DATUM);
  spr.pushSprite(0, 0);
}

// ============================================================
// TELA 1 — Dashboard Principal
// ============================================================
void drawScreen1() {
  spr.fillSprite(COL_BG);

  drawHeader("HW MON");

  int barX = 70;
  int barW = 175;
  int barH = 16;
  int startY = 36;
  int spacing = 24;

  drawMetric("CPU",  hw.cpu,  barX, startY,              barW, barH, COL_CYAN,    "%");
  drawMetric("GPU",  hw.gpu,  barX, startY + spacing,     barW, barH, COL_MAGENTA, "%");
  drawMetric("RAM",  hw.ram,  barX, startY + spacing * 2, barW, barH, COL_GREEN,   "%");

  // FPS — número grande e destacado
  int fpsY = startY + spacing * 3 + 2;
  spr.setTextColor(COL_YELLOW);
  spr.setTextSize(1);
  spr.setTextDatum(MR_DATUM);
  spr.drawString("FPS", barX - 8, fpsY + 8);

  if (hw.fps > 0) {
    char fpsBuf[8];
    snprintf(fpsBuf, sizeof(fpsBuf), "%d", hw.fps);
    spr.setTextColor(COL_YELLOW);
    spr.setTextSize(3);
    spr.setTextDatum(ML_DATUM);
    spr.drawString(fpsBuf, barX, fpsY - 2);
  } else {
    spr.setTextColor(COL_DIM);
    spr.setTextSize(2);
    spr.setTextDatum(ML_DATUM);
    spr.drawString("---", barX, fpsY);
  }

  // Scanline quando cpu_temp ou gpu_temp > 80
  int maxTemp = max(hw.cpu_temp, hw.gpu_temp);
  scanlineActive = (maxTemp > 80);
  if (scanlineActive) {
    drawScanlineEffect();
  }

  drawPageIndicator();
  spr.pushSprite(0, 0);
}

// ============================================================
// TELA 2 — Temperaturas & Detalhes
// ============================================================
void drawScreen2() {
  spr.fillSprite(COL_BG);

  drawHeader("TEMPS & INFO");

  int barX = 90;
  int barW = 170;
  int barH = 16;
  int startY = 36;
  int spacing = 24;

  drawMetric("CPU T", hw.cpu_temp, barX, startY,           barW, barH, COL_CYAN,   "C");
  drawMetric("GPU T", hw.gpu_temp, barX, startY + spacing,  barW, barH, COL_MAGENTA,"C");

  // CPU Clock
  int lineY = startY + spacing * 2 + 4;
  char clkBuf[16];

  spr.setTextColor(COL_CYAN);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);
  spr.drawString("CPU CLK", 10, lineY);
  snprintf(clkBuf, sizeof(clkBuf), "%d MHz", hw.cpu_clk);
  spr.setTextColor(COL_TEXT);
  spr.setTextDatum(TR_DATUM);
  spr.drawString(clkBuf, SCREEN_W - 10, lineY);

  // GPU Clock
  lineY += 18;
  spr.setTextColor(COL_MAGENTA);
  spr.setTextDatum(TL_DATUM);
  spr.drawString("GPU CLK", 10, lineY);
  snprintf(clkBuf, sizeof(clkBuf), "%d MHz", hw.gpu_clk);
  spr.setTextColor(COL_TEXT);
  spr.setTextDatum(TR_DATUM);
  spr.drawString(clkBuf, SCREEN_W - 10, lineY);

  // Hardware info na parte inferior
  lineY += 22;
  spr.setTextColor(COL_DIM);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);
  spr.drawString("i5-12400F / RX 9060 XT", 10, lineY);

  // Scanline quando temp > 80
  int maxTemp = max(hw.cpu_temp, hw.gpu_temp);
  scanlineActive = (maxTemp > 80);
  if (scanlineActive) {
    drawScanlineEffect();
  }

  drawPageIndicator();
  spr.pushSprite(0, 0);
}

// ============================================================
// TELA 3 — Gaming (FPS grande + temps)
// ============================================================
void drawScreen3() {
  spr.fillSprite(COL_BG);

  drawHeader("GAMING");

  int cx = SCREEN_W / 2;

  if (hw.fps > 0) {
    // FPS gigante centralizado
    char fpsBuf[8];
    snprintf(fpsBuf, sizeof(fpsBuf), "%d", hw.fps);
    spr.setTextColor(COL_YELLOW);
    spr.setTextSize(7);
    spr.setTextDatum(MC_DATUM);
    spr.drawString(fpsBuf, cx, 80);

    // Label "FPS" abaixo do número
    spr.setTextColor(COL_DIM);
    spr.setTextSize(2);
    spr.drawString("FPS", cx, 115);
  } else {
    spr.setTextColor(COL_DIM);
    spr.setTextSize(4);
    spr.setTextDatum(MC_DATUM);
    spr.drawString("---", cx, 80);

    spr.setTextSize(1);
    spr.drawString("RTSS nao detectado", cx, 110);
  }

  // Temps na parte inferior
  char tempBuf[16];
  int tempY = SCREEN_H - 20;

  // CPU temp à esquerda
  spr.setTextColor(COL_CYAN);
  spr.setTextSize(2);
  spr.setTextDatum(BL_DATUM);
  snprintf(tempBuf, sizeof(tempBuf), "CPU %d%sC", hw.cpu_temp, "\xB0");
  spr.drawString(tempBuf, 10, tempY);

  // GPU temp à direita
  spr.setTextColor(COL_MAGENTA);
  spr.setTextDatum(BR_DATUM);
  snprintf(tempBuf, sizeof(tempBuf), "GPU %d%sC", hw.gpu_temp, "\xB0");
  spr.drawString(tempBuf, SCREEN_W - 10, tempY);

  // Scanline quando temp > 80
  int maxTemp = max(hw.cpu_temp, hw.gpu_temp);
  scanlineActive = (maxTemp > 80);
  if (scanlineActive) {
    drawScanlineEffect();
  }

  drawPageIndicator();
  spr.pushSprite(0, 0);
}

// ============================================================
// HEADER
// ============================================================
void drawHeader(const char* title) {
  // Linha decorativa superior
  spr.drawFastHLine(0, 0, SCREEN_W, COL_DIM);

  // Título
  spr.setTextSize(2);
  spr.setTextDatum(TL_DATUM);

  if (strcmp(title, "HW MON") == 0) {
    spr.setTextColor(COL_CYAN);
    spr.drawString("HW", 8, 8);
    spr.setTextColor(COL_MAGENTA);
    spr.drawString("MON", 38, 8);
  } else {
    spr.setTextColor(COL_ORANGE);
    spr.drawString(title, 8, 8);
  }

  // Horário
  spr.setTextColor(COL_TEXT);
  spr.setTextSize(2);
  spr.setTextDatum(TR_DATUM);
  spr.drawString(hw.hora, SCREEN_W - 28, 8);

  // Indicador de status (bolinha verde pulsante)
  uint8_t pulse = (millis() / 500) % 2;
  spr.fillCircle(SCREEN_W - 10, 15, 5, pulse ? COL_GREEN : 0x03E0);

  // Linha separadora
  spr.drawFastHLine(0, 30, SCREEN_W, COL_DIM);
}

// ============================================================
// MÉTRICA COM BARRA DE PROGRESSO
// ============================================================
void drawMetric(const char* label, int value, int x, int y,
                int w, int h, uint16_t color, const char* unit) {

  // Label à esquerda
  spr.setTextColor(color);
  spr.setTextSize(1);
  spr.setTextDatum(MR_DATUM);
  spr.drawString(label, x - 8, y + h / 2);

  // Fundo da barra
  spr.fillRoundRect(x, y, w, h, 3, COL_BAR_BG);

  // Cálculo do preenchimento
  int fillW = 0;
  if (strcmp(unit, "C") == 0) {
    fillW = map(constrain(value, 0, 100), 0, 100, 0, w);
  } else {
    fillW = map(value, 0, 100, 0, w);
  }

  if (fillW > 0) {
    spr.fillRoundRect(x, y, fillW, h, 3, color);

    // Highlight no topo da barra (efeito 3D)
    uint16_t lighter = lightenColor(color);
    spr.drawFastHLine(x + 2, y + 1, fillW - 4, lighter);
  }

  // Valor numérico à direita da barra
  char buf[12];
  if (strcmp(unit, "C") == 0) {
    snprintf(buf, sizeof(buf), "%d%sC", value, "\xB0");
  } else {
    snprintf(buf, sizeof(buf), "%d%%", value);
  }

  spr.setTextColor(COL_TEXT);
  spr.setTextSize(1);
  spr.setTextDatum(ML_DATUM);
  spr.drawString(buf, x + w + 6, y + h / 2);

  // Indicador de alerta quando valor > 90
  if (value > 90) {
    if ((millis() / 300) % 2) {
      spr.fillCircle(x - 20, y + h / 2, 3, COL_RED);
    }
  }
}

// ============================================================
// EFEITO SCANLINE (quando temp > 80°C)
// ============================================================
void drawScanlineEffect() {
  scanlineOffset = (scanlineOffset + 1) % 4;
  for (int y = scanlineOffset; y < SCREEN_H; y += 4) {
    spr.drawFastHLine(0, y, SCREEN_W, COL_SCANLINE);
  }

  // Efeito "glitch" sutil
  if ((millis() / 200) % 7 == 0) {
    int glitchY = random(30, SCREEN_H - 10);
    int glitchH = random(2, 6);
    int shift   = random(-3, 4);
    for (int gy = glitchY; gy < glitchY + glitchH && gy < SCREEN_H; gy++) {
      if (shift != 0) {
        spr.drawFastHLine(shift > 0 ? shift : 0, gy,
                          SCREEN_W - abs(shift), COL_SCANLINE);
      }
    }
  }
}

// ============================================================
// INDICADOR DE PÁGINA (pequeno, canto inferior direito)
// ============================================================
void drawPageIndicator() {
  // Bolinhas indicadoras de página no canto inferior direito
  int dotY = SCREEN_H - 8;
  int totalW = (NUM_SCREENS - 1) * 12;
  int dotStartX = SCREEN_W - 10 - totalW;
  for (int i = 0; i < NUM_SCREENS; i++) {
    int dx = dotStartX + i * 12;
    if (i == currentScreen) {
      spr.fillCircle(dx, dotY, 3, COL_TEXT);
    } else {
      spr.drawCircle(dx, dotY, 3, COL_DIM);
    }
  }
}

// ============================================================
// UTILITÁRIOS
// ============================================================
uint16_t lightenColor(uint16_t color) {
  uint8_t r = (color >> 11) & 0x1F;
  uint8_t g = (color >> 5)  & 0x3F;
  uint8_t b =  color        & 0x1F;

  r = min(31, r + 8);
  g = min(63, g + 16);
  b = min(31, b + 8);

  return (r << 11) | (g << 5) | b;
}

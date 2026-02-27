// ============================================================
// HW Monitor v3 — Firmware para Lilygo T-Display-S3
// ESP32-S3 + Display ST7789 1.9" (170x320)
// Auto-switch: Idle (pixel art + clock) / Gaming (FPS + temps)
// ============================================================

#include <TFT_eSPI.h>
#include <ArduinoJson.h>

// ── Protótipos ──────────────────────────────────────────────
void readSerial();
void parseJson(const String &json);
void drawOfflineScreen();
void drawIdleScreen();
void drawGamingScreen();
void drawCat(int x, int y, int scale, int frame);
void drawZzz(int x, int y, int frame);
uint16_t lightenColor(uint16_t color);

// ── Display ─────────────────────────────────────────────────
TFT_eSPI    tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

static const int SCREEN_W = 320;
static const int SCREEN_H = 170;

// ── Paleta (RGB565) ─────────────────────────────────────────
static const uint16_t COL_BG       = TFT_BLACK;
static const uint16_t COL_CYAN     = 0x07FF;
static const uint16_t COL_MAGENTA  = 0xF81F;
static const uint16_t COL_GREEN    = 0x07E0;
static const uint16_t COL_ORANGE   = 0xFDA0;
static const uint16_t COL_YELLOW   = 0xFFE0;
static const uint16_t COL_TEXT     = 0xFFFF;
static const uint16_t COL_DIM      = 0x7BEF;
static const uint16_t COL_RED      = 0xF800;
static const uint16_t COL_SCANLINE = 0x0821;

// Cores do gato
static const uint16_t COL_CAT_BODY   = 0x7BEF;  // cinza
static const uint16_t COL_CAT_DARK   = 0x4A49;  // cinza escuro (listras/orelhas)
static const uint16_t COL_CAT_NOSE   = 0xFB2C;  // rosa
static const uint16_t COL_CAT_EYE    = 0x07E0;  // verde

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
  char hora[6]  = "--:--";
  char data[12] = "";
};

HWData hw;

// ── Timeout ─────────────────────────────────────────────────
unsigned long lastDataTime = 0;
static const unsigned long TIMEOUT_MS = 5000;
bool isOffline  = true;
bool wasOffline = true;

// ── Serial buffer ───────────────────────────────────────────
String serialBuffer = "";

// ── Scanline ────────────────────────────────────────────────
int scanlineOffset = 0;

// ── Gaming mode cooldown ────────────────────────────────────
bool inGamingMode = false;
unsigned long lastFpsTime = 0;
static const unsigned long GAMING_COOLDOWN_MS = 3000;

// ── Animação idle ───────────────────────────────────────────
unsigned long idleAnimTimer = 0;
int idleFrame = 0;
int zzzFrame  = 0;

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  pinMode(15, OUTPUT);
  digitalWrite(15, HIGH);

  tft.init();
  tft.setRotation(1);

  pinMode(38, OUTPUT);
  digitalWrite(38, HIGH);

  tft.fillScreen(COL_BG);

  spr.createSprite(SCREEN_W, SCREEN_H);
  spr.setTextDatum(TL_DATUM);

  lastDataTime = millis();
  drawOfflineScreen();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  readSerial();

  // Auto-switch gaming/idle
  if (hw.fps > 0) {
    inGamingMode = true;
    lastFpsTime = millis();
  } else if (inGamingMode && (millis() - lastFpsTime > GAMING_COOLDOWN_MS)) {
    inGamingMode = false;
  }

  // Timeout check
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
    if (inGamingMode) {
      drawGamingScreen();
    } else {
      drawIdleScreen();
    }
    wasOffline = false;
  }

  delay(50);
}

// ============================================================
// SERIAL + JSON
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
      if (serialBuffer.length() > 512) {
        serialBuffer = "";
      }
    }
  }
}

void parseJson(const String &json) {
  StaticJsonDocument<384> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) return;

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

  const char* d = doc["date"] | "";
  strncpy(hw.data, d, sizeof(hw.data) - 1);
  hw.data[sizeof(hw.data) - 1] = '\0';

  lastDataTime = millis();
  isOffline    = false;
}

// ============================================================
// TELA OFFLINE
// ============================================================
void drawOfflineScreen() {
  spr.fillSprite(COL_BG);

  int cx = SCREEN_W / 2;
  int cy = SCREEN_H / 2 - 15;
  for (int i = -2; i <= 2; i++) {
    spr.drawLine(cx - 20 + i, cy - 20, cx + 20 + i, cy + 20, COL_RED);
    spr.drawLine(cx + 20 + i, cy - 20, cx - 20 + i, cy + 20, COL_RED);
  }
  spr.drawCircle(cx, cy, 30, COL_DIM);

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
// TELA IDLE — Pixel art cat + relógio
// ============================================================
void drawIdleScreen() {
  spr.fillSprite(COL_BG);

  // Avança animação a cada 800ms
  if (millis() - idleAnimTimer > 800) {
    idleAnimTimer = millis();
    idleFrame = (idleFrame + 1) % 2;
    zzzFrame  = (zzzFrame + 1) % 4;
  }

  // ── Gato dormindo (esquerda da tela) ──
  int catX = 20;
  int catY = 35;
  int scale = 4;
  drawCat(catX, catY, scale, idleFrame);
  drawZzz(catX + 14 * scale, catY - 4 * scale, zzzFrame);

  // ── Relógio grande (direita) ──
  int clockX = 190;

  spr.setTextColor(COL_TEXT);
  spr.setTextSize(5);
  spr.setTextDatum(MC_DATUM);
  spr.drawString(hw.hora, clockX + 35, 70);

  // Data abaixo
  if (strlen(hw.data) > 0) {
    spr.setTextColor(COL_DIM);
    spr.setTextSize(2);
    spr.drawString(hw.data, clockX + 35, 105);
  }

  // ── Info sutil no rodapé ──
  spr.setTextColor(COL_DIM);
  spr.setTextSize(1);
  spr.setTextDatum(BL_DATUM);

  char infoBuf[32];
  snprintf(infoBuf, sizeof(infoBuf), "CPU %d%%  RAM %d%%", hw.cpu, hw.ram);
  spr.drawString(infoBuf, 8, SCREEN_H - 4);

  if (hw.cpu_temp > 0 || hw.gpu_temp > 0) {
    char tempBuf[32];
    snprintf(tempBuf, sizeof(tempBuf), "%d%sC / %d%sC",
             hw.cpu_temp, "\xB0", hw.gpu_temp, "\xB0");
    spr.setTextDatum(BR_DATUM);
    spr.drawString(tempBuf, SCREEN_W - 8, SCREEN_H - 4);
  }

  spr.pushSprite(0, 0);
}

// ============================================================
// GATO PIXEL ART — "cat loaf" dormindo, vista lateral
// Grid: ~20 wide x 13 tall, escala = s pixels por pixel-art
// Frame 0: normal, Frame 1: respiração (corpo sobe 1px)
// ============================================================
void drawCat(int ox, int oy, int s, int frame) {
  int b = (frame == 1) ? -s : 0;  // breath offset

  #define PX(x, y, col)  spr.fillRect(ox + (x)*s, oy + (y)*s, s, s, col)
  #define PXB(x, y, col) spr.fillRect(ox + (x)*s, oy + (y)*s + b, s, s, col)

  // ── Orelhas (juntas no topo, triângulo) ──
  PX(3, 0, COL_CAT_DARK);                          // orelha esq ponta
  PX(6, 0, COL_CAT_DARK);                          // orelha dir ponta
  PX(2, 1, COL_CAT_DARK); PX(3, 1, COL_CAT_BODY); PX(4, 1, COL_CAT_DARK);  // orelha esq
  PX(5, 1, COL_CAT_DARK); PX(6, 1, COL_CAT_BODY); PX(7, 1, COL_CAT_DARK);  // orelha dir

  // ── Cabeça (respira) ──
  for (int x = 1; x <= 8; x++) PXB(x, 2, COL_CAT_BODY);   // topo cabeça
  for (int x = 0; x <= 9; x++) PXB(x, 3, COL_CAT_BODY);   // meio cabeça
  for (int x = 0; x <= 9; x++) PXB(x, 4, COL_CAT_BODY);   // meio cabeça
  for (int x = 1; x <= 8; x++) PXB(x, 5, COL_CAT_BODY);   // queixo

  // Olhos fechados (arcos curvos ── )
  PXB(2, 3, COL_CAT_DARK); PXB(3, 3, COL_CAT_DARK);       // olho esq
  PXB(6, 3, COL_CAT_DARK); PXB(7, 3, COL_CAT_DARK);       // olho dir

  // Nariz + boca
  PXB(4, 4, COL_CAT_NOSE); PXB(5, 4, COL_CAT_NOSE);       // nariz rosa
  PXB(3, 5, COL_CAT_DARK); PXB(6, 5, COL_CAT_DARK);       // boca

  // Bigodes
  PXB(0, 3, COL_CAT_DARK);                                  // bigode esq
  PXB(9, 3, COL_CAT_DARK);                                  // bigode dir
  PXB(0, 4, COL_CAT_DARK);                                  // bigode esq baixo
  PXB(9, 4, COL_CAT_DARK);                                  // bigode dir baixo

  // ── Corpo (largo, achatado — "cat loaf") ──
  for (int x = 0; x <= 15; x++) PXB(x, 6, COL_CAT_BODY);
  for (int x = 0; x <= 16; x++) PXB(x, 7, COL_CAT_BODY);
  for (int x = 0; x <= 16; x++) PXB(x, 8, COL_CAT_BODY);
  for (int x = 0; x <= 16; x++) PXB(x, 9, COL_CAT_BODY);
  for (int x = 1; x <= 15; x++) PXB(x, 10, COL_CAT_BODY);

  // Listras no corpo
  PXB(5, 7, COL_CAT_DARK);  PXB(9, 7, COL_CAT_DARK);  PXB(13, 7, COL_CAT_DARK);
  PXB(5, 8, COL_CAT_DARK);  PXB(9, 8, COL_CAT_DARK);  PXB(13, 8, COL_CAT_DARK);

  // ── Patinhas da frente (dobradas, descansando) ──
  PX(1, 11, COL_CAT_BODY); PX(2, 11, COL_CAT_BODY); PX(3, 11, COL_CAT_BODY);

  // ── Rabo (curva elegante pra cima, saindo de trás) ──
  PXB(16, 9, COL_CAT_DARK);
  PX(17, 8, COL_CAT_DARK);
  PX(18, 7, COL_CAT_DARK);
  PX(18, 6, COL_CAT_DARK);
  PX(17, 5, COL_CAT_DARK);  // ponta do rabo curvando

  #undef PX
  #undef PXB
}

// ============================================================
// ZZZ FLUTUANTE
// ============================================================
void drawZzz(int ox, int oy, int frame) {
  // 3 "z"s em posições que flutuam
  int offsets[] = {0, -3, -6, -3};  // oscilação vertical
  int yOff = offsets[frame];

  spr.setTextColor(COL_DIM);
  spr.setTextDatum(TL_DATUM);

  spr.setTextSize(1);
  spr.drawString("z", ox, oy + yOff + 12);

  spr.setTextSize(1);
  spr.drawString("z", ox + 8, oy + yOff + 4);

  spr.setTextSize(2);
  spr.drawString("Z", ox + 16, oy + yOff - 6);
}

// ============================================================
// TELA GAMING — FPS grande + temps
// ============================================================
void drawGamingScreen() {
  spr.fillSprite(COL_BG);

  // ── Header ──
  spr.drawFastHLine(0, 0, SCREEN_W, COL_DIM);

  spr.setTextSize(2);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(COL_ORANGE);
  spr.drawString("GAMING", 8, 8);

  spr.setTextColor(COL_TEXT);
  spr.setTextDatum(TR_DATUM);
  spr.drawString(hw.hora, SCREEN_W - 28, 8);

  uint8_t pulse = (millis() / 500) % 2;
  spr.fillCircle(SCREEN_W - 10, 15, 5, pulse ? COL_GREEN : 0x03E0);

  spr.drawFastHLine(0, 30, SCREEN_W, COL_DIM);

  // ── FPS gigante ──
  int cx = SCREEN_W / 2;

  if (hw.fps > 0) {
    char fpsBuf[8];
    snprintf(fpsBuf, sizeof(fpsBuf), "%d", hw.fps);
    spr.setTextColor(COL_YELLOW);
    spr.setTextSize(7);
    spr.setTextDatum(MC_DATUM);
    spr.drawString(fpsBuf, cx, 78);

    spr.setTextColor(COL_DIM);
    spr.setTextSize(2);
    spr.drawString("FPS", cx, 113);
  }

  // ── Temps embaixo ──
  char tempBuf[16];
  int tempY = SCREEN_H - 20;

  spr.setTextColor(COL_CYAN);
  spr.setTextSize(2);
  spr.setTextDatum(BL_DATUM);
  snprintf(tempBuf, sizeof(tempBuf), "CPU %d%sC", hw.cpu_temp, "\xB0");
  spr.drawString(tempBuf, 10, tempY);

  spr.setTextColor(COL_MAGENTA);
  spr.setTextDatum(BR_DATUM);
  snprintf(tempBuf, sizeof(tempBuf), "GPU %d%sC", hw.gpu_temp, "\xB0");
  spr.drawString(tempBuf, SCREEN_W - 10, tempY);

  // ── Scanline quando temp > 80 ──
  int maxTemp = max(hw.cpu_temp, hw.gpu_temp);
  if (maxTemp > 80) {
    scanlineOffset = (scanlineOffset + 1) % 4;
    for (int y = scanlineOffset; y < SCREEN_H; y += 4) {
      spr.drawFastHLine(0, y, SCREEN_W, COL_SCANLINE);
    }
  }

  spr.pushSprite(0, 0);
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

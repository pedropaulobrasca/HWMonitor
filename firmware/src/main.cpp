// ============================================================
// HW Monitor v4 — Firmware para Lilygo T-Display-S3
// ESP32-S3 + Display ST7789 1.9" (170x320)
// WiFi provisioning via captive portal + NTP
// Auto-switch: Idle (pixel art + clock) / Gaming (FPS + temps)
// ============================================================

#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>

// ── NTP ─────────────────────────────────────────────────────
static const char* NTP_SERVER   = "pool.ntp.org";
static const long  GMT_OFFSET   = -3 * 3600;  // BRT (Brasília) = UTC-3
static const int   DST_OFFSET   = 0;

// ── WiFi Manager ─────────────────────────────────────────────
static const char* AP_NAME = "HWMonitor";
WiFiManager wm;

// ── Protótipos ──────────────────────────────────────────────
void setupWiFi();
void syncNTP();
void updateNtpTime();
void readSerial();
void parseJson(const String &json);
void drawBootScreen(const char* msg);
void drawConfigScreen();
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
static const uint16_t COL_CAT_BODY = 0x7BEF;
static const uint16_t COL_CAT_DARK = 0x4A49;
static const uint16_t COL_CAT_NOSE = 0xFB2C;

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

// ── Estado da conexão serial ────────────────────────────────
unsigned long lastDataTime = 0;
static const unsigned long SERIAL_TIMEOUT_MS = 5000;
bool hasSerialData = false;

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

// ── NTP time ────────────────────────────────────────────────
bool ntpSynced = false;
unsigned long lastNtpUpdate = 0;
static const unsigned long NTP_UPDATE_INTERVAL = 60000;  // atualiza a cada 60s

// ── WiFi ────────────────────────────────────────────────────
bool wifiConnected = false;

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

  // Boot screen: conectando WiFi
  drawBootScreen("Conectando WiFi...");
  setupWiFi();

  if (wifiConnected) {
    drawBootScreen("Sincronizando relogio...");
    syncNTP();
  } else {
    drawConfigScreen();
  }

  lastDataTime = millis();
}

// ============================================================
// WIFI (provisioning via captive portal)
// ============================================================
void setupWiFi() {
  WiFi.mode(WIFI_STA);

  wm.setConfigPortalBlocking(false);
  wm.setConnectTimeout(10);
  wm.setSaveConnectTimeout(10);

  // Tenta conectar com credenciais salvas, senão abre portal
  if (wm.autoConnect(AP_NAME)) {
    wifiConnected = true;
  } else {
    // Portal aberto — AP "HWMonitor" no ar
    wifiConnected = false;
  }
}

// ============================================================
// NTP
// ============================================================
void syncNTP() {
  configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 5000)) {
    ntpSynced = true;
  }
}

void updateNtpTime() {
  // Atualiza hora/data do NTP local (sem rede, struct tm é mantido pelo ESP32)
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 0)) {
    strftime(hw.hora, sizeof(hw.hora), "%H:%M", &timeinfo);
    strftime(hw.data, sizeof(hw.data), "%d %b", &timeinfo);
  }
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  // Se WiFi não conectou, processa o portal captive
  if (!wifiConnected) {
    wm.process();

    // Checa se conectou agora
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      drawBootScreen("Sincronizando relogio...");
      syncNTP();
    } else {
      // Redesenha tela de config periodicamente (animação)
      static unsigned long lastConfigDraw = 0;
      if (millis() - lastConfigDraw > 500) {
        lastConfigDraw = millis();
        drawConfigScreen();
      }
      delay(50);
      return;
    }
  }

  // Reconecta WiFi se caiu
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    WiFi.reconnect();
  }

  readSerial();

  // Verifica se serial está ativa
  bool serialActive = (millis() - lastDataTime < SERIAL_TIMEOUT_MS) && hasSerialData;

  // Se não tem serial, usa hora do NTP
  if (!serialActive && ntpSynced) {
    if (millis() - lastNtpUpdate > NTP_UPDATE_INTERVAL) {
      lastNtpUpdate = millis();
      updateNtpTime();
    }
    // Atualiza hora a cada frame se não tem serial
    updateNtpTime();
  }

  // Auto-switch gaming/idle
  if (hw.fps > 0 && serialActive) {
    inGamingMode = true;
    lastFpsTime = millis();
  } else if (inGamingMode && (millis() - lastFpsTime > GAMING_COOLDOWN_MS)) {
    inGamingMode = false;
  }

  // Sempre mostra algo: gaming ou idle (nunca "offline")
  if (inGamingMode) {
    drawGamingScreen();
  } else {
    drawIdleScreen();
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

  const char* t = doc["time"] | "";
  if (strlen(t) > 0) {
    strncpy(hw.hora, t, sizeof(hw.hora) - 1);
    hw.hora[sizeof(hw.hora) - 1] = '\0';
  }

  const char* d = doc["date"] | "";
  if (strlen(d) > 0) {
    strncpy(hw.data, d, sizeof(hw.data) - 1);
    hw.data[sizeof(hw.data) - 1] = '\0';
  }

  lastDataTime = millis();
  hasSerialData = true;
}

// ============================================================
// BOOT SCREEN
// ============================================================
void drawBootScreen(const char* msg) {
  spr.fillSprite(COL_BG);

  int cx = SCREEN_W / 2;
  spr.setTextColor(COL_CYAN);
  spr.setTextSize(2);
  spr.setTextDatum(MC_DATUM);
  spr.drawString("HW MON", cx, 60);

  spr.setTextColor(COL_DIM);
  spr.setTextSize(1);
  spr.drawString(msg, cx, 90);

  spr.setTextDatum(TL_DATUM);
  spr.pushSprite(0, 0);
}

// ============================================================
// TELA CONFIG — Portal captive ativo, instrui o usuário
// ============================================================
void drawConfigScreen() {
  spr.fillSprite(COL_BG);

  int cx = SCREEN_W / 2;
  spr.setTextDatum(MC_DATUM);

  // Ícone WiFi piscando
  uint8_t pulse = (millis() / 600) % 2;
  spr.setTextColor(pulse ? COL_CYAN : COL_DIM);
  spr.setTextSize(2);
  spr.drawString("WiFi Setup", cx, 25);

  // Instruções
  spr.setTextColor(COL_TEXT);
  spr.setTextSize(2);
  spr.drawString("Conecte na rede:", cx, 58);

  spr.setTextColor(COL_YELLOW);
  spr.setTextSize(3);
  spr.drawString(AP_NAME, cx, 88);

  spr.setTextColor(COL_DIM);
  spr.setTextSize(1);
  spr.drawString("Abra o navegador em 192.168.4.1", cx, 118);
  spr.drawString("e selecione sua rede WiFi", cx, 132);

  // Bolinha animada
  int dotX = cx - 15 + ((millis() / 300) % 3) * 15;
  spr.fillCircle(dotX, 152, 3, COL_CYAN);

  spr.pushSprite(0, 0);
}

// ============================================================
// TELA IDLE — Pixel art cat + relógio (funciona sem PC!)
// ============================================================
void drawIdleScreen() {
  spr.fillSprite(COL_BG);

  // Avança animação a cada 800ms
  if (millis() - idleAnimTimer > 800) {
    idleAnimTimer = millis();
    idleFrame = (idleFrame + 1) % 2;
    zzzFrame  = (zzzFrame + 1) % 4;
  }

  // ── Gato dormindo (esquerda) ──
  int catX = 20;
  int catY = 35;
  int scale = 4;
  drawCat(catX, catY, scale, idleFrame);
  drawZzz(catX + 14 * scale, catY - 4 * scale, zzzFrame);

  // ── Relógio grande (direita) ──
  int clockX = 225;

  spr.setTextColor(COL_TEXT);
  spr.setTextSize(5);
  spr.setTextDatum(MC_DATUM);
  spr.drawString(hw.hora, clockX, 70);

  // Data abaixo
  if (strlen(hw.data) > 0) {
    spr.setTextColor(COL_DIM);
    spr.setTextSize(2);
    spr.drawString(hw.data, clockX, 105);
  }

  // ── Rodapé: info do PC (se disponível) ou status WiFi ──
  bool serialActive = (millis() - lastDataTime < SERIAL_TIMEOUT_MS) && hasSerialData;

  spr.setTextColor(COL_DIM);
  spr.setTextSize(1);

  if (serialActive) {
    char infoBuf[32];
    snprintf(infoBuf, sizeof(infoBuf), "CPU %d%%  RAM %d%%", hw.cpu, hw.ram);
    spr.setTextDatum(BL_DATUM);
    spr.drawString(infoBuf, 8, SCREEN_H - 4);

    if (hw.cpu_temp > 0 || hw.gpu_temp > 0) {
      char tempBuf[32];
      snprintf(tempBuf, sizeof(tempBuf), "%d%sC / %d%sC",
               hw.cpu_temp, "\xB0", hw.gpu_temp, "\xB0");
      spr.setTextDatum(BR_DATUM);
      spr.drawString(tempBuf, SCREEN_W - 8, SCREEN_H - 4);
    }
  } else {
    // Sem serial: mostra status WiFi
    spr.setTextDatum(BR_DATUM);
    if (wifiConnected) {
      spr.drawString("WiFi OK", SCREEN_W - 8, SCREEN_H - 4);
    } else {
      spr.setTextColor(COL_RED);
      spr.drawString("WiFi OFF", SCREEN_W - 8, SCREEN_H - 4);
    }
  }

  spr.pushSprite(0, 0);
}

// ============================================================
// GATO PIXEL ART — "cat loaf" dormindo, vista lateral
// ============================================================
void drawCat(int ox, int oy, int s, int frame) {
  int b = (frame == 1) ? -s : 0;

  #define PX(x, y, col)  spr.fillRect(ox + (x)*s, oy + (y)*s, s, s, col)
  #define PXB(x, y, col) spr.fillRect(ox + (x)*s, oy + (y)*s + b, s, s, col)

  // ── Orelhas ──
  PX(3, 0, COL_CAT_DARK);
  PX(6, 0, COL_CAT_DARK);
  PX(2, 1, COL_CAT_DARK); PX(3, 1, COL_CAT_BODY); PX(4, 1, COL_CAT_DARK);
  PX(5, 1, COL_CAT_DARK); PX(6, 1, COL_CAT_BODY); PX(7, 1, COL_CAT_DARK);

  // ── Cabeça ──
  for (int x = 1; x <= 8; x++) PXB(x, 2, COL_CAT_BODY);
  for (int x = 0; x <= 9; x++) PXB(x, 3, COL_CAT_BODY);
  for (int x = 0; x <= 9; x++) PXB(x, 4, COL_CAT_BODY);
  for (int x = 1; x <= 8; x++) PXB(x, 5, COL_CAT_BODY);

  // Olhos fechados
  PXB(2, 3, COL_CAT_DARK); PXB(3, 3, COL_CAT_DARK);
  PXB(6, 3, COL_CAT_DARK); PXB(7, 3, COL_CAT_DARK);

  // Nariz + boca
  PXB(4, 4, COL_CAT_NOSE); PXB(5, 4, COL_CAT_NOSE);
  PXB(3, 5, COL_CAT_DARK); PXB(6, 5, COL_CAT_DARK);

  // Bigodes
  PXB(0, 3, COL_CAT_DARK); PXB(9, 3, COL_CAT_DARK);
  PXB(0, 4, COL_CAT_DARK); PXB(9, 4, COL_CAT_DARK);

  // ── Corpo ──
  for (int x = 0; x <= 15; x++) PXB(x, 6, COL_CAT_BODY);
  for (int x = 0; x <= 16; x++) PXB(x, 7, COL_CAT_BODY);
  for (int x = 0; x <= 16; x++) PXB(x, 8, COL_CAT_BODY);
  for (int x = 0; x <= 16; x++) PXB(x, 9, COL_CAT_BODY);
  for (int x = 1; x <= 15; x++) PXB(x, 10, COL_CAT_BODY);

  // Listras
  PXB(5, 7, COL_CAT_DARK);  PXB(9, 7, COL_CAT_DARK);  PXB(13, 7, COL_CAT_DARK);
  PXB(5, 8, COL_CAT_DARK);  PXB(9, 8, COL_CAT_DARK);  PXB(13, 8, COL_CAT_DARK);

  // ── Patinhas ──
  PX(1, 11, COL_CAT_BODY); PX(2, 11, COL_CAT_BODY); PX(3, 11, COL_CAT_BODY);

  // ── Rabo ──
  PXB(16, 9, COL_CAT_DARK);
  PX(17, 8, COL_CAT_DARK);
  PX(18, 7, COL_CAT_DARK);
  PX(18, 6, COL_CAT_DARK);
  PX(17, 5, COL_CAT_DARK);

  #undef PX
  #undef PXB
}

// ============================================================
// ZZZ FLUTUANTE
// ============================================================
void drawZzz(int ox, int oy, int frame) {
  int offsets[] = {0, -3, -6, -3};
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

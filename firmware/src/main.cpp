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
#include <HTTPClient.h>
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
void fetchLocation();
void fetchWeather();
void readSerial();
void parseJson(const String &json);
void drawBootScreen(const char* msg);
void drawConfigScreen();
void drawIdleScreen();
void drawGamingScreen();
void drawHeart(int x, int y, int scale, int frame);
void drawWeatherIcon(int ox, int oy, int s, int code);
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

// Cores do coração
static const uint16_t COL_HEART     = 0xF810;  // vermelho/rosa vibrante
static const uint16_t COL_HEART_LT  = 0xFB2C;  // rosa claro (brilho)
static const uint16_t COL_HEART_DK  = 0xC000;  // vermelho escuro (sombra)

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

// ── NTP time ────────────────────────────────────────────────
bool ntpSynced = false;
unsigned long lastNtpUpdate = 0;
static const unsigned long NTP_UPDATE_INTERVAL = 60000;  // atualiza a cada 60s

// ── WiFi ────────────────────────────────────────────────────
bool wifiConnected = false;

// ── Clima ────────────────────────────────────────────────────
float weatherLat = 0, weatherLon = 0;
int   weatherTemp = 0;
int   weatherCode = -1;
bool  weatherValid = false;
unsigned long lastWeatherUpdate = 0;
static const unsigned long WEATHER_INTERVAL = 900000;  // 15 min

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
    drawBootScreen("Buscando clima...");
    fetchLocation();
    fetchWeather();
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
// CLIMA — geolocalização por IP + Open-Meteo
// ============================================================
void fetchLocation() {
  HTTPClient http;
  http.begin("http://ip-api.com/json/?fields=lat,lon");
  http.setTimeout(5000);

  int code = http.GET();
  if (code == 200) {
    StaticJsonDocument<128> doc;
    if (!deserializeJson(doc, http.getString())) {
      weatherLat = doc["lat"] | 0.0f;
      weatherLon = doc["lon"] | 0.0f;
    }
  }
  http.end();
}

void fetchWeather() {
  if (weatherLat == 0 && weatherLon == 0) return;

  char url[160];
  snprintf(url, sizeof(url),
    "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
    "&current=temperature_2m,weather_code",
    weatherLat, weatherLon);

  HTTPClient http;
  http.begin(url);
  http.setTimeout(5000);

  int code = http.GET();
  if (code == 200) {
    StaticJsonDocument<512> doc;
    if (!deserializeJson(doc, http.getString())) {
      weatherTemp = (int)round((float)(doc["current"]["temperature_2m"] | 0.0));
      weatherCode = doc["current"]["weather_code"] | -1;
      weatherValid = true;
      lastWeatherUpdate = millis();
    }
  }
  http.end();
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
      drawBootScreen("Buscando clima...");
      fetchLocation();
      fetchWeather();
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

  // Atualiza clima a cada 15 min
  if (wifiConnected && (millis() - lastWeatherUpdate > WEATHER_INTERVAL)) {
    fetchWeather();
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

  // Avança animação de batida do coração
  if (millis() - idleAnimTimer > 600) {
    idleAnimTimer = millis();
    idleFrame = (idleFrame + 1) % 4;  // 0=normal, 1=grande, 2=normal, 3=pequeno
  }

  // ── Coração + "Pa" (esquerda) ──
  int heartX = 25;
  int heartY = 18;
  int heartScale = 4;
  drawHeart(heartX, heartY, heartScale, idleFrame);

  // Nome "Pa" abaixo do coração
  spr.setTextColor(COL_HEART_LT);
  spr.setTextSize(3);
  spr.setTextDatum(MC_DATUM);
  spr.drawString("Pa", heartX + 5 * heartScale, heartY + 12 * heartScale);

  // ── Relógio grande (direita) ──
  int clockX = 225;

  spr.setTextColor(COL_TEXT);
  spr.setTextSize(4);
  spr.setTextDatum(MC_DATUM);
  spr.drawString(hw.hora, clockX, 38);

  // Data abaixo
  if (strlen(hw.data) > 0) {
    spr.setTextColor(COL_DIM);
    spr.setTextSize(2);
    spr.drawString(hw.data, clockX, 68);
  }

  // ── Clima ──
  if (weatherValid) {
    int weatherY = 105;
    // Ícone pixel art
    drawWeatherIcon(clockX - 40, weatherY - 12, 3, weatherCode);

    // Temperatura
    char wBuf[12];
    snprintf(wBuf, sizeof(wBuf), "%d%sC", weatherTemp, "\xB0");
    spr.setTextColor(COL_YELLOW);
    spr.setTextSize(2);
    spr.setTextDatum(ML_DATUM);
    spr.drawString(wBuf, clockX - 2, weatherY);
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
// CORAÇÃO PIXEL ART — com animação de batida
// Grid: 11 wide x 10 tall
// Frames: 0,2=normal  1=expand  3=shrink
// ============================================================
void drawHeart(int ox, int oy, int s, int frame) {
  // Offset para animação de batida
  int expand = 0;
  if (frame == 1) expand = 1;       // batida: cresce
  else if (frame == 3) expand = -1;  // contrai levemente

  int adj = -expand;  // offset de posição (centraliza a escala)
  int es = s + expand; // tamanho efetivo do pixel (não menor que s-1)
  if (es < s - 1) es = s - 1;

  #define HP(x, y, col) spr.fillRect(ox + (x)*s + adj, oy + (y)*s + adj, es, es, col)

  // Linha 0: topos dos dois "bumps"
  //    ##  ##
  HP(1,0,COL_HEART); HP(2,0,COL_HEART); HP(3,0,COL_HEART);
  HP(7,0,COL_HEART); HP(8,0,COL_HEART); HP(9,0,COL_HEART);

  // Linha 1: expande
  // #######.####
  for (int x = 0; x <= 10; x++) HP(x, 1, COL_HEART);

  // Linha 2-4: cheio
  for (int x = 0; x <= 10; x++) HP(x, 2, COL_HEART);
  for (int x = 0; x <= 10; x++) HP(x, 3, COL_HEART);
  for (int x = 1; x <= 9; x++)  HP(x, 4, COL_HEART);

  // Linha 5-8: afunilando
  for (int x = 2; x <= 8; x++) HP(x, 5, COL_HEART);
  for (int x = 3; x <= 7; x++) HP(x, 6, COL_HEART);
  for (int x = 4; x <= 6; x++) HP(x, 7, COL_HEART);
  HP(5, 8, COL_HEART);

  // Brilho (canto superior esquerdo)
  HP(2, 1, COL_HEART_LT); HP(3, 1, COL_HEART_LT);
  HP(1, 2, COL_HEART_LT); HP(2, 2, COL_HEART_LT);

  // Sombra (borda inferior direita)
  HP(9, 3, COL_HEART_DK);
  HP(8, 4, COL_HEART_DK); HP(9, 4, COL_HEART_DK);
  HP(7, 5, COL_HEART_DK); HP(8, 5, COL_HEART_DK);
  HP(6, 6, COL_HEART_DK); HP(7, 6, COL_HEART_DK);
  HP(5, 7, COL_HEART_DK); HP(6, 7, COL_HEART_DK);

  #undef HP
}

// ============================================================
// ÍCONE CLIMA — pixel art procedural (WMO weather codes)
// ============================================================
void drawWeatherIcon(int ox, int oy, int s, int code) {
  #define WP(x, y, col) spr.fillRect(ox + (x)*s, oy + (y)*s, s, s, col)

  if (code <= 1) {
    // ── Sol ──
    uint16_t SUN = COL_YELLOW;
    // Centro
    WP(3,2,SUN); WP(4,2,SUN);
    WP(2,3,SUN); WP(3,3,SUN); WP(4,3,SUN); WP(5,3,SUN);
    WP(2,4,SUN); WP(3,4,SUN); WP(4,4,SUN); WP(5,4,SUN);
    WP(3,5,SUN); WP(4,5,SUN);
    // Raios
    WP(3,0,SUN); WP(4,0,SUN);
    WP(0,3,SUN); WP(7,3,SUN);
    WP(0,4,SUN); WP(7,4,SUN);
    WP(3,7,SUN); WP(4,7,SUN);
    WP(1,1,SUN); WP(6,1,SUN);
    WP(1,6,SUN); WP(6,6,SUN);

  } else if (code == 2) {
    // ── Sol + nuvem ──
    uint16_t SUN = COL_YELLOW;
    uint16_t CLD = COL_DIM;
    // Sol pequeno (canto superior direito)
    WP(5,0,SUN); WP(6,0,SUN);
    WP(5,1,SUN); WP(6,1,SUN);
    WP(7,0,SUN); WP(4,1,SUN);
    // Nuvem na frente
    WP(2,3,CLD); WP(3,3,CLD); WP(4,3,CLD); WP(5,3,CLD);
    WP(1,4,CLD); WP(2,4,CLD); WP(3,4,CLD); WP(4,4,CLD); WP(5,4,CLD); WP(6,4,CLD);
    WP(1,5,CLD); WP(2,5,CLD); WP(3,5,CLD); WP(4,5,CLD); WP(5,5,CLD); WP(6,5,CLD);

  } else if (code == 3 || (code >= 45 && code <= 48)) {
    // ── Nublado / neblina ──
    uint16_t CLD = COL_DIM;
    WP(2,1,CLD); WP(3,1,CLD); WP(4,1,CLD); WP(5,1,CLD);
    WP(1,2,CLD); WP(2,2,CLD); WP(3,2,CLD); WP(4,2,CLD); WP(5,2,CLD); WP(6,2,CLD);
    WP(1,3,CLD); WP(2,3,CLD); WP(3,3,CLD); WP(4,3,CLD); WP(5,3,CLD); WP(6,3,CLD);
    WP(0,4,CLD); WP(1,4,CLD); WP(2,4,CLD); WP(3,4,CLD); WP(4,4,CLD); WP(5,4,CLD); WP(6,4,CLD); WP(7,4,CLD);
    WP(0,5,CLD); WP(1,5,CLD); WP(2,5,CLD); WP(3,5,CLD); WP(4,5,CLD); WP(5,5,CLD); WP(6,5,CLD); WP(7,5,CLD);

  } else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    // ── Chuva ──
    uint16_t CLD = COL_DIM;
    uint16_t DRP = COL_CYAN;
    // Nuvem
    WP(2,0,CLD); WP(3,0,CLD); WP(4,0,CLD); WP(5,0,CLD);
    WP(1,1,CLD); WP(2,1,CLD); WP(3,1,CLD); WP(4,1,CLD); WP(5,1,CLD); WP(6,1,CLD);
    WP(0,2,CLD); WP(1,2,CLD); WP(2,2,CLD); WP(3,2,CLD); WP(4,2,CLD); WP(5,2,CLD); WP(6,2,CLD); WP(7,2,CLD);
    // Gotas
    WP(1,4,DRP); WP(3,4,DRP); WP(5,4,DRP);
    WP(2,5,DRP); WP(4,5,DRP); WP(6,5,DRP);
    WP(1,6,DRP); WP(3,6,DRP); WP(5,6,DRP);

  } else if (code >= 71 && code <= 77) {
    // ── Neve ──
    uint16_t CLD = COL_DIM;
    uint16_t SNW = COL_TEXT;
    // Nuvem
    WP(2,0,CLD); WP(3,0,CLD); WP(4,0,CLD); WP(5,0,CLD);
    WP(1,1,CLD); WP(2,1,CLD); WP(3,1,CLD); WP(4,1,CLD); WP(5,1,CLD); WP(6,1,CLD);
    WP(0,2,CLD); WP(1,2,CLD); WP(2,2,CLD); WP(3,2,CLD); WP(4,2,CLD); WP(5,2,CLD); WP(6,2,CLD); WP(7,2,CLD);
    // Flocos
    WP(2,4,SNW); WP(5,4,SNW);
    WP(1,5,SNW); WP(4,5,SNW); WP(7,5,SNW);
    WP(3,6,SNW); WP(6,6,SNW);

  } else if (code >= 95) {
    // ── Trovoada ──
    uint16_t CLD = COL_DIM;
    uint16_t ZAP = COL_YELLOW;
    uint16_t DRP = COL_CYAN;
    // Nuvem
    WP(2,0,CLD); WP(3,0,CLD); WP(4,0,CLD); WP(5,0,CLD);
    WP(1,1,CLD); WP(2,1,CLD); WP(3,1,CLD); WP(4,1,CLD); WP(5,1,CLD); WP(6,1,CLD);
    WP(0,2,CLD); WP(1,2,CLD); WP(2,2,CLD); WP(3,2,CLD); WP(4,2,CLD); WP(5,2,CLD); WP(6,2,CLD); WP(7,2,CLD);
    // Raio
    WP(4,3,ZAP); WP(3,4,ZAP); WP(4,4,ZAP); WP(5,4,ZAP);
    WP(3,5,ZAP); WP(4,5,ZAP); WP(2,6,ZAP);
    // Gotas
    WP(1,4,DRP); WP(6,5,DRP);

  } else {
    // Fallback: nuvem genérica
    uint16_t CLD = COL_DIM;
    WP(2,1,CLD); WP(3,1,CLD); WP(4,1,CLD); WP(5,1,CLD);
    WP(1,2,CLD); WP(2,2,CLD); WP(3,2,CLD); WP(4,2,CLD); WP(5,2,CLD); WP(6,2,CLD);
    WP(0,3,CLD); WP(1,3,CLD); WP(2,3,CLD); WP(3,3,CLD); WP(4,3,CLD); WP(5,3,CLD); WP(6,3,CLD); WP(7,3,CLD);
    WP(0,4,CLD); WP(1,4,CLD); WP(2,4,CLD); WP(3,4,CLD); WP(4,4,CLD); WP(5,4,CLD); WP(6,4,CLD); WP(7,4,CLD);
  }

  #undef WP
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

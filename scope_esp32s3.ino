#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h> 
#include <SPI.h>
#include <TFT_eSPI.h>
#include <Adafruit_NeoPixel.h>

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite menuSpr = TFT_eSprite(&tft); 
Preferences prefs;
WebServer server(80); 
Adafruit_NeoPixel led(1, 48, NEO_GRB + NEO_KHZ800);

// --- НАСТРОЙКИ КНОПОК И ПИЩАЛКИ ---
#define BTN_UP    41
#define BTN_DOWN  40
#define BTN_LEFT  39
#define BTN_RIGHT 37
#define BTN_OK    36

#define TEST_WAVE_PIN 42  
#define TFT_BL_PIN    38     
#define BUZZER_PIN    35     // <-- ПИН ПИЩАЛКИ (замени, если используешь другой)

bool lastStateUP    = HIGH;
bool lastStateDOWN  = HIGH;
bool lastStateLEFT  = HIGH;
bool lastStateRIGHT = HIGH;
bool lastStateOK    = HIGH;

// --- НАСТРОЙКА ПИНОВ И ЦВЕТОВ ---
const int adcPins[] = {4, 5, 9}; 
const int NUM_ADC_PINS = 3;
bool pinEnabled[NUM_ADC_PINS] = {true, true, true}; 
int pinColorIndices[NUM_ADC_PINS] = {5, 2, 6};

const uint16_t avaliableColors[] = {TFT_WHITE, TFT_RED, TFT_GREEN, TFT_BLUE, TFT_YELLOW, TFT_CYAN, TFT_MAGENTA};
const char* colorNames[] = {"WHITE", "RED", "GREEN", "BLUE", "YELLOW", "CYAN", "MAGENTA"};

// --- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ГРАФИКА ---
volatile float voltage = 0.0;
int xPos = 0;
int prevX = 0;
int prevYPins[NUM_ADC_PINS] = {100, 100, 100};

const int WAVE_BUFFER_SIZE = 60;
int waveBuffer[WAVE_BUFFER_SIZE];
int waveBufferIdx = 0;

volatile int currentMinAdc = 4095;
volatile int currentMaxAdc = 0;
volatile int lastMinAdc = 0;
volatile int lastMaxAdc = 4095;
volatile int crossings = 0;
unsigned long sweepStartTime = 0;
volatile float freqHz = 0.0;
volatile float vMax = 0.0;
volatile float vMin = 0.0;
bool isFrozen = false; 
bool wasAboveMidpoint = false;

// Параметры меню
int graphSpeed;
int smoothLevel;     
int zoomIndex;       
bool useGrid;
int ledMode;         
int ledColorIndex;   
int testFreqIndex;   
bool wifiEnabled = true; 
int brightnessIndex = 0;    
volatile bool generatorChanged = false;

int triggerMode = 0;    
int couplingMode = 0;   
int vDivIndex = 1;      
int themeColorIdx = 0;  
bool buzzerEnabled = false;

const int zoomLevels[] = {1, 2, 4, 8};
const char* smoothNames[] = {"DISABLED", "LIGHT", "MEDIUM", "STRICT"};
const char* ledModes[] = {"DISABLED", "STATIC", "RAINBOW", "SWEEP"};
const char* ledColorNames[] = {"WHITE", "RED", "GREEN", "BLUE", "YELLOW", "CYAN", "MAGENTA"};
const int brightnessLevels[] = {255, 128, 50};
const char* brightnessNames[] = {"100%", "50%", "20%"};
const int testFreqs[] = {10, 50, 100, 500, 1000, 5000, 10000, 100000};
const char* freqNames[] = {"10 Hz", "50 Hz", "100 Hz", "500 Hz", "1 kHz", "5 kHz", "10 kHz", "100 kHz"};
const int NUM_FREQS = 8;
const uint8_t ledRGBValues[][3] = {{255,255,255},{255,0,0},{0,255,0},{0,0,255},{255,255,0},{0,255,255},{255,0,255}};

const uint16_t themeColors[] = {TFT_CYAN, TFT_YELLOW, TFT_RED, TFT_GREEN};
const char* themeNames[] = {"CYAN Neon", "GOLD Cyber", "RED Plasma", "GREEN Matrix"};

uint8_t rainbowHue = 0;
unsigned long lastLedUpdate = 0;
int sweepDirection = 1;
int sweepVal = 0;

// --- ПЕРЕМЕННЫЕ МЕНЮ И АНИМАЦИИ ---
int currentMode = 0; 
int menuCursor = 0;
int menuScrollOffset = 0; 
const int MAX_VISIBLE_ITEMS = 4; 
const int MENU_ITEMS_COUNT = 17;

float currentScrollY = 0.0; 
float currentVisualY = 2.0;
float channelsScrollY = 0.0;
float channelsVisualY = 2.0;

int pulseState = 0;
int pulseDir = 1;
int pinsSubCursor = 0;
int wifiSubCursor = 0;

// ТАЙМЕР ХИНКАЛИ
int khinkaliTimerSec = 420; 
bool khinkaliRunning = false;
unsigned long lastKhinkaliSecTick = 0;
int steamAnimFrame = 0;

// ПРОТОТИПЫ
void loadSettings(); void saveSettings(); void handleButtons(); void drawOscilloscope();
void drawGrid(); void updateLED(); void handleLEDAnimations(); uint32_t ledWheel(uint8_t wheelPos);
void initMenuScreen(); void updateMenuDisplay(); void updatePinsMenuDisplay(); void updateWiFiMenuDisplay();
void updateGenerator(); void toggleWiFi(); void updateBrightness(); void drawKhinkaliScreen();

// --- ФУНКЦИЯ ПИЩАЛКИ ---
void beep(int freq, int duration) {
  if (buzzerEnabled) {
    tone(BUZZER_PIN, freq, duration);
  }
}

// --- ВЕБ ИНТЕРФЕЙС ---
const char PAGE_MAIN[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>Quantum Scope UI</title>
<style>
:root { --bg: #05050a; --card: #0d0d1f; --accent: #00ffcc; --magenta: #ff007f; --text: #e0e0ff; }
body { background: var(--bg); color: var(--text); font-family: 'Segoe UI', sans-serif; margin: 0; padding: 20px; display: flex; flex-direction: column; align-items: center; }
h2 { color: var(--accent); text-shadow: 0 0 10px rgba(0,255,204,0.5); font-weight: 400; letter-spacing: 2px; }
#waveCanvas { background: #020205; border: 2px solid #1a1a3a; border-radius: 16px; box-shadow: 0 0 20px rgba(0,255,204,0.15); width: 100%; max-width: 550px; }
.grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 15px; width: 100%; max-width: 550px; margin: 20px 0; }
.card { background: var(--card); padding: 15px; border-radius: 12px; text-align: center; border: 1px solid #1a1a3a; box-shadow: inset 0 0 10px rgba(255,255,255,0.02); }
.card .label { font-size: 11px; color: #62628a; text-transform: uppercase; letter-spacing: 1px; }
.card .val { font-size: 22px; font-weight: bold; margin-top: 6px; font-family: monospace; }
.wifi-info { margin-top: 15px; font-size: 13px; color: #555577; text-align: center; border-top: 1px solid #111; padding-top: 10px; }
</style></head><body>
<h2>⚡ QUANTUM SCOPE OS ⚡</h2>
<canvas id="waveCanvas" width="550" height="240"></canvas>
<div class='grid'>
<div class='card'><div class='label'>⚡ Amplitude</div><div class='val' id='v' style='color:var(--accent)'>-- V</div></div>
<div class='card'><div class='label'>🔄 Frequency</div><div class='val' id='freq' style='color:var(--magenta)'>-- Hz</div></div>
</div>
<div class='wifi-info'>Connected to AP: ESP32-Scope | Control Center v2.5</div>
<script>
let canvas = document.getElementById('waveCanvas'); let ctx = canvas.getContext('2d');
setInterval(async () => {
try {
let r = await fetch('/data'); let j = await r.json();
document.getElementById('v').innerText = j.v.toFixed(2) + ' V';
document.getElementById('freq').innerText = j.freq >= 1000 ? (j.freq/1000).toFixed(2) + ' kHz' : j.freq.toFixed(0) + ' Hz';
ctx.clearRect(0, 0, canvas.width, canvas.height);
ctx.strokeStyle = '#0a0a1f'; ctx.lineWidth = 1; ctx.beginPath();
for(let m=1; m<4; m++) { ctx.moveTo(0, canvas.height*m/4); ctx.lineTo(canvas.width, canvas.height*m/4); } ctx.stroke();
ctx.strokeStyle = '#00ffcc'; ctx.lineWidth = 2.5; ctx.shadowBlur = 8; ctx.shadowColor = '#00ffcc'; ctx.beginPath();
for(let i=0; i<j.wave.length; i++) {
let x = (i / (j.wave.length-1)) * canvas.width; let y = canvas.height - ((j.wave[i] / 4095) * canvas.height);
if(i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
} ctx.stroke(); ctx.shadowBlur = 0;
}catch(e){} }, 150);
</script></body></html>)rawliteral";

void handleRoot() { server.send_P(200, "text/html", PAGE_MAIN); }
void handleData() {
  String json = "{\"v\":" + String(voltage) + ",\"freq\":" + String(freqHz) + ",\"wave\":[";
  for (int i = 0; i < WAVE_BUFFER_SIZE; i++) { json += String(waveBuffer[i]); if (i < WAVE_BUFFER_SIZE - 1) json += ","; }
  json += "]}"; server.send(200, "application/json", json);
}

void core0Task(void * pvParameters) {
  if (wifiEnabled) { 
    WiFi.softAP("ESP32-Scope", "12345678"); 
    server.on("/", handleRoot); 
    server.on("/data", handleData); 
    server.begin(); 
  }
  for (;;) { if (wifiEnabled) server.handleClient(); vTaskDelay(5 / portTICK_PERIOD_MS); }
}

void setup() {
  Serial.begin(115200);
  delay(500); 
  Serial.println("\n--- QUANTUM SCOPE OS INITIALIZATION ---");

  pinMode(BTN_UP, INPUT_PULLUP); pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP); pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);
  pinMode(TFT_BL_PIN, OUTPUT); pinMode(TEST_WAVE_PIN, OUTPUT); 
  pinMode(BUZZER_PIN, OUTPUT);
  
  loadSettings();
  led.begin(); led.setBrightness(50); updateBrightness();
  
  xTaskCreatePinnedToCore(core0Task, "Core0Task", 8192, NULL, 1, NULL, 0);
  updateGenerator(); 
  
  tft.init(); tft.setRotation(1); tft.fillScreen(TFT_BLACK);
  menuSpr.createSprite(tft.width(), 106);
  
  sweepStartTime = millis(); updateLED(); 
  Serial.println("[SYSTEM] Setup Complete. Mega-OS Ready.");
}

void loop() {
  handleButtons(); 
  handleLEDAnimations(); 

  if (generatorChanged) { 
    generatorChanged = false; 
    updateGenerator(); 
  }
  
  if (currentMode == 0) {
    if (!isFrozen) drawOscilloscope();
    if (graphSpeed == 0) delayMicroseconds(10); 
  } else {
    if (currentMode == 1) {
      updateMenuDisplay(); 
    } else if (currentMode == 4) {
      updatePinsMenuDisplay(); 
    } else if (currentMode == 5) {
      drawKhinkaliScreen();
    }
    delay(16); 
  }
}

void initMenuScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(themeColors[themeColorIdx], TFT_BLACK);
  tft.drawString("⚡ QUANTUM CONFIG ⚡", 50, 5, 2); 
  tft.drawFastHLine(0, 24, tft.width(), tft.color565(40, 40, 80));
  tft.drawFastHLine(0, 132, tft.width(), tft.color565(40, 40, 80));
}

void updateMenuDisplay() {
  if (menuCursor < menuScrollOffset) menuScrollOffset = menuCursor;
  else if (menuCursor >= menuScrollOffset + MAX_VISIBLE_ITEMS) menuScrollOffset = menuCursor - MAX_VISIBLE_ITEMS + 1;

  float targetScrollY = menuScrollOffset * 26.0;
  currentScrollY += (targetScrollY - currentScrollY) * 0.20; 
  float targetVisualY = (menuCursor * 26.0) - currentScrollY + 2.0;
  currentVisualY += (targetVisualY - currentVisualY) * 0.30; 

  pulseState += pulseDir * 4;
  if (pulseState >= 50 || pulseState <= 0) pulseDir = -pulseDir;

  menuSpr.fillSprite(TFT_BLACK);
  uint16_t sliderColor = menuSpr.color565(40 + pulseState, 40 + pulseState, 90 + pulseState / 2);
  menuSpr.fillRect(4, (int)currentVisualY, menuSpr.width() - 8, 22, sliderColor);

  for (int i = 0; i < MENU_ITEMS_COUNT; i++) {
    float yTextPos = 5.0 + (i * 26.0) - currentScrollY;
    if (yTextPos < -20 || yTextPos > 110) continue; 

    if (i == menuCursor) menuSpr.setTextColor(TFT_GREEN, sliderColor);
    else menuSpr.setTextColor(TFT_WHITE, TFT_BLACK);

    String itemText = "";
    switch(i) {
      case 0:  itemText = "-> CHANNELS CONFIG"; break;
      case 1:  itemText = "ZOOM: " + String(zoomLevels[zoomIndex]) + "x"; break;
      case 2:  itemText = "FILT: " + String(smoothNames[smoothLevel]); break;
      case 3:  itemText = "SPEED: " + String(graphSpeed) + "ms"; break;
      case 4:  itemText = "GRID: " + String(useGrid ? "ON" : "OFF"); break;
      case 5:  itemText = "TRIGGER: " + String(triggerMode == 0 ? "AUTO" : "NORMAL"); break;
      case 6:  itemText = "COUPLING: " + String(couplingMode == 0 ? "DC" : "AC"); break;
      case 7:  itemText = "V-DIV: " + String(vDivIndex == 0 ? "0.5V" : vDivIndex == 1 ? "1.0V" : "2.0V"); break;
      case 8:  itemText = "THEME: " + String(themeNames[themeColorIdx]); break;
      case 9:  itemText = "BUZZER: " + String(buzzerEnabled ? "ON" : "OFF"); break;
      case 10: itemText = "GEN: " + String(freqNames[testFreqIndex]); break; 
      case 11: itemText = "LED M: " + String(ledModes[ledMode]); break;
      case 12: itemText = "LED C: " + String(ledColorNames[ledColorIndex]); break;
      case 13: itemText = "BRIGHT: " + String(brightnessNames[brightnessIndex]); break;
      case 14: itemText = "-> WI-FI SETTINGS"; break;
      case 15: itemText = "🥟 KHINKALI COOKER"; break;
      case 16: itemText = "[ SAVE & EXIT ]"; break;
    }
    menuSpr.drawString(itemText, 15, (int)yTextPos, 2);
  }
  menuSpr.pushSprite(0, 25);
}

void updatePinsMenuDisplay() {
  float targetScrollY = 0.0; 
  float targetVisualY = (pinsSubCursor * 26.0) + 2.0;
  channelsVisualY += (targetVisualY - channelsVisualY) * 0.30;

  menuSpr.fillSprite(TFT_BLACK);
  menuSpr.fillRect(4, (int)channelsVisualY, menuSpr.width() - 8, 22, menuSpr.color565(30, 60, 50));

  for (int i = 0; i < 4; i++) {
    float yTextPos = 5.0 + (i * 26.0);
    if (i == pinsSubCursor) menuSpr.setTextColor(TFT_GREEN, menuSpr.color565(30, 60, 50));
    else menuSpr.setTextColor(TFT_WHITE, TFT_BLACK);

    if (i < 3) {
      String name = (i==0)?"CH4 ":(i==1)?"CH5 ":"CH9 ";
      String status = pinEnabled[i] ? "[ON]" : "[OFF]";
      String colName = colorNames[pinColorIndices[i]];
      menuSpr.drawString(name + status + " | COLOR: " + colName, 15, (int)yTextPos, 2);
    } else {
      menuSpr.drawString("[ BACK ]", 15, (int)yTextPos, 2);
    }
  }
  menuSpr.pushSprite(0, 25);
}

void updateWiFiMenuDisplay() {
  tft.fillScreen(TFT_BLACK); 
  tft.setTextColor(themeColors[themeColorIdx], TFT_BLACK); 
  tft.drawString("=== WI-FI CONTROL ===", 55, 8, 2);
  tft.drawFastHLine(0, 26, tft.width(), themeColors[themeColorIdx]);

  if (wifiSubCursor == 0) { tft.fillRect(4, 38, tft.width()-8, 22, tft.color565(0,60,60)); tft.setTextColor(TFT_GREEN, tft.color565(0,60,60)); }
  else tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(wifiEnabled ? "MODEM: ACTIVE" : "MODEM: DISABLED", 12, 41, 2);
  
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("SSID: ESP32-Scope", 12, 68, 2);
  tft.drawString("PASS: 12345678", 12, 86, 2);
  tft.setTextColor(tft.color565(0, 255, 150), TFT_BLACK);
  tft.drawString("IP:   192.168.4.1", 12, 104, 2);

  if (wifiSubCursor == 1) { tft.fillRect(4, 132, tft.width()-8, 22, tft.color565(40,40,40)); tft.setTextColor(TFT_GREEN, tft.color565(40,40,40)); }
  else tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("[ BACK TO MENU ]", 12, 135, 2);
}

void drawKhinkaliScreen() {
  if (khinkaliRunning && millis() - lastKhinkaliSecTick >= 1000) {
    lastKhinkaliSecTick = millis();
    if (khinkaliTimerSec > 0) {
      khinkaliTimerSec--;
      steamAnimFrame = (steamAnimFrame + 1) % 3;
    }
  }
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("🥟 KHINKALI COOKING PROFILE 🥟", 15, 8, 2);
  tft.drawFastHLine(0, 26, tft.width(), TFT_YELLOW);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  if (khinkaliRunning && khinkaliTimerSec > 0) {
    if (steamAnimFrame == 0) tft.drawString("  ~   ~   ~  ", 100, 42, 2);
    else if (steamAnimFrame == 1) tft.drawString("   ~   ~   ~ ", 100, 42, 2);
    else tft.drawString(" ~   ~   ~   ", 100, 42, 2);
  }
  tft.setTextColor(tft.color565(200, 180, 140), TFT_BLACK);
  tft.drawString("    ( ( 🥟 ) )    ", 80, 60, 2);

  int mins = khinkaliTimerSec / 60; int secs = khinkaliTimerSec % 60;
  char timeStr[16]; snprintf(timeStr, sizeof(timeStr), "%02d:%02d", mins, secs);
  tft.setTextColor(TFT_CYAN, TFT_BLACK); tft.drawString("TIME REMAINING:", 25, 102, 2);
  
  if (khinkaliTimerSec == 0 && khinkaliRunning) {
    tft.setTextColor(TFT_RED, TFT_BLACK); tft.drawString("!! READY !!", 150, 102, 2);
    // Сигнализация о готовности
    if (millis() % 1000 < 500) beep(1000, 200);
  } else {
    tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.drawString(timeStr, 160, 102, 2);
  }
}

void handleButtons() {
  bool rUP    = digitalRead(BTN_UP);
  bool rDOWN  = digitalRead(BTN_DOWN);
  bool rLEFT  = digitalRead(BTN_LEFT);
  bool rRIGHT = digitalRead(BTN_RIGHT);
  bool rOK    = digitalRead(BTN_OK);

  // --- КНОПКА OK ---
  if (rOK == LOW && lastStateOK == HIGH) {
    beep(2000, 30);
    delay(30);
    if (currentMode == 0) { 
      currentMode = 1; menuCursor = 0; menuScrollOffset = 0; initMenuScreen(); 
    }
    else if (currentMode == 1) {
      if (menuCursor == 0) { currentMode = 4; pinsSubCursor = 0; tft.fillScreen(TFT_BLACK); tft.drawString("=== CHANNELS ===", 80, 8, 2); }
      else if (menuCursor == 14) { currentMode = 3; wifiSubCursor = 0; updateWiFiMenuDisplay(); }
      else if (menuCursor == 15) { currentMode = 5; khinkaliTimerSec = 420; khinkaliRunning = false; } 
      else if (menuCursor == 16) { currentMode = 0; tft.fillScreen(TFT_BLACK); xPos = 0; }
    }
    else if (currentMode == 4) { 
      if (pinsSubCursor < 3) { 
        pinColorIndices[pinsSubCursor] = (pinColorIndices[pinsSubCursor] + 1) % 7; saveSettings();
      } else { currentMode = 1; initMenuScreen(); } 
    }
    else if (currentMode == 3) { 
      if (wifiSubCursor == 0) toggleWiFi(); else { currentMode = 1; initMenuScreen(); } 
    }
  }
  lastStateOK = rOK;

  // --- КНОПКА ВВЕРХ ---
  if (rUP == LOW && lastStateUP == HIGH) {
    beep(2000, 30);
    delay(30); 
    if (currentMode == 0) { isFrozen = !isFrozen; }
    else if (currentMode == 1) { menuCursor--; if (menuCursor < 0) menuCursor = MENU_ITEMS_COUNT - 1; }
    else if (currentMode == 4) { pinsSubCursor--; if (pinsSubCursor < 0) pinsSubCursor = 3; }
    else if (currentMode == 3) { wifiSubCursor--; if (wifiSubCursor < 0) wifiSubCursor = 1; updateWiFiMenuDisplay(); }
    else if (currentMode == 5) { khinkaliRunning = !khinkaliRunning; if (khinkaliRunning) lastKhinkaliSecTick = millis(); }
  }
  lastStateUP = rUP;

  // --- КНОПКА ВНИЗ ---
  if (rDOWN == LOW && lastStateDOWN == HIGH) {
    beep(2000, 30);
    delay(30);
    if (currentMode == 1) { menuCursor++; if (menuCursor >= MENU_ITEMS_COUNT) menuCursor = 0; }
    else if (currentMode == 4) { pinsSubCursor = (pinsSubCursor + 1) % 4; }
    else if (currentMode == 3) { wifiSubCursor = (wifiSubCursor + 1) % 2; updateWiFiMenuDisplay(); }
    else if (currentMode == 5) { currentMode = 1; khinkaliRunning = false; initMenuScreen(); } 
  }
  lastStateDOWN = rDOWN;

  // --- КНОПКА ПРАВО ---
  if (rRIGHT == LOW && lastStateRIGHT == HIGH) {
    beep(2000, 30);
    delay(30);
    if (currentMode == 0) { graphSpeed += 5; if (graphSpeed > 30) graphSpeed = 30; }
    else if (currentMode == 1) {
      if (menuCursor == 1) zoomIndex = (zoomIndex + 1) % 4;
      else if (menuCursor == 2) smoothLevel = (smoothLevel + 1) % 4;
      else if (menuCursor == 3) graphSpeed = (graphSpeed + 5) % 35;
      else if (menuCursor == 4) useGrid = !useGrid;
      else if (menuCursor == 5) triggerMode = (triggerMode + 1) % 2;
      else if (menuCursor == 6) couplingMode = (couplingMode + 1) % 2;
      else if (menuCursor == 7) vDivIndex = (vDivIndex + 1) % 3;
      else if (menuCursor == 8) { themeColorIdx = (themeColorIdx + 1) % 4; initMenuScreen(); }
      else if (menuCursor == 9) buzzerEnabled = !buzzerEnabled;
      else if (menuCursor == 10) { testFreqIndex = (testFreqIndex + 1) % NUM_FREQS; generatorChanged = true; }
      else if (menuCursor == 11) { ledMode = (ledMode + 1) % 4; updateLED(); }
      else if (menuCursor == 12) { ledColorIndex = (ledColorIndex + 1) % 7; updateLED(); }
      else if (menuCursor == 13) { brightnessIndex = (brightnessIndex + 1) % 3; updateBrightness(); }
      saveSettings();
    }
    else if (currentMode == 4) { 
      if (pinsSubCursor < 3) { pinEnabled[pinsSubCursor] = !pinEnabled[pinsSubCursor]; saveSettings(); }
    }
  }
  lastStateRIGHT = rRIGHT;

  // --- КНОПКА ЛЕВО ---
  if (rLEFT == LOW && lastStateLEFT == HIGH) {
    beep(2000, 30);
    delay(30);
    if (currentMode == 0) { graphSpeed -= 5; if (graphSpeed < 0) graphSpeed = 0; } 
    else if (currentMode == 1) {
      if (menuCursor == 1) zoomIndex = (zoomIndex - 1 + 4) % 4;
      else if (menuCursor == 2) smoothLevel = (smoothLevel - 1 + 4) % 4;
      else if (menuCursor == 3) { graphSpeed -= 5; if (graphSpeed < 0) graphSpeed = 30; }
      else if (menuCursor == 4) useGrid = !useGrid;
      else if (menuCursor == 5) triggerMode = (triggerMode - 1 + 2) % 2;
      else if (menuCursor == 6) couplingMode = (couplingMode - 1 + 2) % 2;
      else if (menuCursor == 7) vDivIndex = (vDivIndex - 1 + 3) % 3;
      else if (menuCursor == 8) { themeColorIdx = (themeColorIdx - 1 + 4) % 4; initMenuScreen(); }
      else if (menuCursor == 9) buzzerEnabled = !buzzerEnabled;
      else if (menuCursor == 10) { testFreqIndex = (testFreqIndex - 1 + NUM_FREQS) % NUM_FREQS; generatorChanged = true; }
      else if (menuCursor == 11) { ledMode = (ledMode - 1 + 4) % 4; updateLED(); }
      else if (menuCursor == 12) { ledColorIndex = (ledColorIndex - 1 + 7) % 7; updateLED(); }
      else if (menuCursor == 13) { brightnessIndex = (brightnessIndex - 1 + 3) % 3; updateBrightness(); }
      saveSettings();
    }
  }
  lastStateLEFT = rLEFT;
}

void toggleWiFi() { 
  wifiEnabled = !wifiEnabled; 
  if (wifiEnabled) { WiFi.softAP("ESP32-Scope", "12345678"); server.begin(); } 
  else { WiFi.softAPdisconnect(true); } 
  saveSettings(); 
}

void updateBrightness() { analogWrite(TFT_BL_PIN, brightnessLevels[brightnessIndex]); }

void updateGenerator() {
  uint32_t freq = testFreqs[testFreqIndex];
  uint8_t bits = (freq <= 100) ? 14 : (freq <= 10000) ? 10 : 8;
  uint32_t duty = (1 << bits) / 2; 
  #if ESP_IDF_VERSION_MAJOR >= 5
    ledcDetach(TEST_WAVE_PIN); delay(2); ledcAttach(TEST_WAVE_PIN, freq, bits);
    ledcWrite(TEST_WAVE_PIN, duty);
  #else
    ledcSetup(0, freq, bits); ledcAttachPin(TEST_WAVE_PIN, 0); ledcWrite(0, duty); 
  #endif
}

void loadSettings() {
  prefs.begin("oscin", true);
  pinEnabled[0] = prefs.getBool("p0_en", true); pinEnabled[1] = prefs.getBool("p1_en", true); pinEnabled[2] = prefs.getBool("p2_en", true);
  pinColorIndices[0] = prefs.getInt("p0_col", 5); pinColorIndices[1] = prefs.getInt("p1_col", 2); pinColorIndices[2] = prefs.getInt("p2_col", 6);
  graphSpeed = prefs.getInt("speed", 10); smoothLevel = prefs.getInt("smooth_l", 2); zoomIndex = prefs.getInt("zoom_idx", 0); useGrid = prefs.getBool("grid", true);
  ledMode = prefs.getInt("lmode", 2); ledColorIndex = prefs.getInt("lcolor", 0); testFreqIndex = prefs.getInt("gen_idx", 2); wifiEnabled = prefs.getBool("wifi_en", true);
  brightnessIndex = prefs.getInt("brg_idx", 0); triggerMode = prefs.getInt("trig", 0); couplingMode = prefs.getInt("coup", 0);
  vDivIndex = prefs.getInt("vdiv", 1); themeColorIdx = prefs.getInt("theme", 0); buzzerEnabled = prefs.getBool("buzz", false);
  prefs.end();
}

void saveSettings() {
  prefs.begin("oscin", false); 
  prefs.putBool("p0_en", pinEnabled[0]); prefs.putBool("p1_en", pinEnabled[1]); prefs.putBool("p2_en", pinEnabled[2]);
  prefs.putInt("p0_col", pinColorIndices[0]); prefs.putInt("p1_col", pinColorIndices[1]); prefs.putInt("p2_col", pinColorIndices[2]);
  prefs.putInt("speed", graphSpeed); prefs.putInt("smooth_l", smoothLevel); prefs.putInt("zoom_idx", zoomIndex); prefs.putBool("grid", useGrid);            
  prefs.putInt("lmode", ledMode); prefs.putInt("lcolor", ledColorIndex); prefs.putInt("gen_idx", testFreqIndex); prefs.putBool("wifi_en", wifiEnabled);     
  prefs.putInt("brg_idx", brightnessIndex); prefs.putInt("trig", triggerMode); prefs.putInt("coup", couplingMode);
  prefs.putInt("vdiv", vDivIndex); prefs.putInt("theme", themeColorIdx); prefs.putBool("buzz", buzzerEnabled); 
  prefs.end();
}

void updateLED() { 
  if (ledMode == 0) { led.setPixelColor(0, 0); led.show(); } 
  else if (ledMode == 1) { led.setPixelColor(0, led.Color(ledRGBValues[ledColorIndex][0], ledRGBValues[ledColorIndex][1], ledRGBValues[ledColorIndex][2])); led.show(); } 
}

void handleLEDAnimations() {
  if (ledMode < 2) return;
  if (millis() - lastLedUpdate >= 20) {
    lastLedUpdate = millis();
    if (ledMode == 2) { led.setPixelColor(0, ledWheel(rainbowHue)); led.show(); rainbowHue += 2; } 
    else if (ledMode == 3) {
      sweepVal += sweepDirection * 4;
      if (sweepVal >= 255) { sweepVal = 255; sweepDirection = -1; } if (sweepVal <= 10) { sweepVal = 10; sweepDirection = 1; }
      led.setPixelColor(0, led.Color((ledRGBValues[ledColorIndex][0]*sweepVal)/255, (ledRGBValues[ledColorIndex][1]*sweepVal)/255, (ledRGBValues[ledColorIndex][2]*sweepVal)/255)); led.show();
    }
  }
}

uint32_t ledWheel(uint8_t wheelPos) { 
  wheelPos = 255 - wheelPos;
  if (wheelPos < 85) return led.Color(255 - wheelPos * 3, 0, wheelPos * 3);
  else if (wheelPos < 170) { wheelPos -= 85; return led.Color(0, wheelPos * 3, 255 - wheelPos * 3); } 
  else { wheelPos -= 170; return led.Color(wheelPos * 3, 255 - wheelPos * 3, 0); } 
}

void drawGrid() { 
  int yTop = 32; int yBottom = tft.height() - 1; int h = yBottom - yTop;
  uint16_t gridColor = tft.color565(40, 40, 50); 
  for (int i = 1; i < 4; i++) tft.drawFastHLine(0, yTop + (h * i) / 4, tft.width(), gridColor);
  for (int i = 1; i < 6; i++) tft.drawFastVLine((tft.width() * i) / 6, yTop, h, gridColor);
}

// ИСПРАВЛЕННЫЙ ОСЦИЛЛОГРАФ (Отцентрированный Zoom и разделение каналов)
void drawOscilloscope() {
  int yTop = 32; 
  int yBottom = tft.height() - 1; 
  bool anyActive = false;
  
  for (int i = 0; i < NUM_ADC_PINS; i++) {
    if (pinEnabled[i]) anyActive = true; 
  }
  
  if (!anyActive) {
    if (xPos == 0) { 
      tft.fillRect(0, yTop, tft.width(), tft.height() - yTop, TFT_BLACK); 
      if (useGrid) drawGrid(); 
      tft.setTextColor(TFT_RED, TFT_BLACK); 
      tft.drawString("NO ACTIVE CH", 70, yTop + 40, 2); 
    }
    xPos++;
    if (xPos >= tft.width()) xPos = 0; 
    if (graphSpeed > 0) delay(graphSpeed); 
    return;
  }
  
  int primaryIdx = 0;
  for (int i = 0; i < NUM_ADC_PINS; i++) { 
    if (pinEnabled[i]) { primaryIdx = i; break; } 
  }
  
  if (xPos == 0 && triggerMode == 1 && (lastMaxAdc - lastMinAdc) < 150) {
    delay(10); 
    return; 
  }

  if (xPos == 0) {
    unsigned long sweepDuration = millis() - sweepStartTime; 
    if (sweepDuration > 0 && (lastMaxAdc - lastMinAdc) > 250 && crossings > 0) {
      freqHz = (crossings * 1000.0) / sweepDuration; 
    } else {
      freqHz = 0.0; 
    }
    vMax = (lastMaxAdc * 3.3) / 4095.0; 
    vMin = (lastMinAdc * 3.3) / 4095.0; 
    
    lastMinAdc = currentMinAdc; 
    lastMaxAdc = currentMaxAdc; 
    currentMinAdc = 4095; 
    currentMaxAdc = 0; 
    crossings = 0; 
    sweepStartTime = millis(); 
    prevX = 0; 
    
    tft.fillRect(0, yTop, tft.width(), tft.height() - yTop, TFT_BLACK); 
    if (useGrid) drawGrid(); 
  }
  
  for (int i = 0; i < NUM_ADC_PINS; i++) {
    if (!pinEnabled[i]) continue; 
    
    int adcValue = 0; 
    int samples = (smoothLevel == 1) ? 5 : (smoothLevel == 2) ? 20 : (smoothLevel == 3) ? 50 : 1; 
    
    if (samples > 1) { 
      long adcTotal = 0;
      for (int s = 0; s < samples; s++) adcTotal += analogRead(adcPins[i]); 
      adcValue = adcTotal / samples;
    } else { 
      adcValue = analogRead(adcPins[i]); 
    }

    if (i == primaryIdx) {
      waveBuffer[waveBufferIdx] = adcValue; 
      if (xPos == 0) waveBufferIdx = (waveBufferIdx + 1) % WAVE_BUFFER_SIZE; 
      
      if (adcValue > currentMaxAdc) currentMaxAdc = adcValue; 
      if (adcValue < currentMinAdc) currentMinAdc = adcValue; 
      
      int range = (lastMaxAdc - lastMinAdc); 
      int midpoint = (lastMinAdc + lastMaxAdc) / 2; 
      int hysteresis = (range / 8 < 40) ? 40 : range / 8; 
      
      if (range > 250) { 
        if (!wasAboveMidpoint && (adcValue > (midpoint + hysteresis))) { 
          crossings++; 
          wasAboveMidpoint = true; 
        } else if (wasAboveMidpoint && (adcValue < (midpoint - hysteresis))) { 
          wasAboveMidpoint = false; 
        } 
      }
      voltage = (adcValue * 3.3) / 4095.0; 
    }
    
    if (couplingMode == 1) {
      int mid = (lastMinAdc + lastMaxAdc) / 2; 
      adcValue = (adcValue - mid) + 2048; 
    }
    
    int zoomedAdc = adcValue;
    
    if (vDivIndex == 0) {
        zoomedAdc = (zoomedAdc - 2048) * 2 + 2048; 
    } else if (vDivIndex == 2) {
        zoomedAdc = (zoomedAdc - 2048) / 2 + 2048; 
    }

    if (zoomLevels[zoomIndex] > 1) {
        zoomedAdc = (zoomedAdc - 2048) * zoomLevels[zoomIndex] + 2048; 
    }

    if (zoomedAdc > 4095) zoomedAdc = 4095; 
    if (zoomedAdc < 0) zoomedAdc = 0; 
    
    int yPos = map(zoomedAdc, 0, 4095, yBottom, yTop); 
    if (xPos == 0) prevYPins[i] = yPos; 
    
    tft.drawLine(prevX, prevYPins[i], xPos, yPos, avaliableColors[pinColorIndices[i]]); 
    prevYPins[i] = yPos; 
  }
  
  tft.setTextColor(themeColors[themeColorIdx], TFT_BLACK); 
  char topText[64]; 
  if (freqHz < 1000.0) {
    snprintf(topText, sizeof(topText), "V:%.1f| ^%.1f _%.1f| %dHz ", voltage, vMax, vMin, (int)freqHz); 
  } else {
    snprintf(topText, sizeof(topText), "V:%.1f| ^%.1f _%.1f| %.1fkHz ", voltage, vMax, vMin, freqHz / 1000.0); 
  }
  tft.drawString(topText, 4, 8, 2); 
  
  prevX = xPos; 
  xPos++; 
  if (xPos >= tft.width()) xPos = 0; 
  if (graphSpeed > 0) delay(graphSpeed); 
}
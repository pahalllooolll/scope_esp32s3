#include <TFT_eSPI.h>
#include <SPI.h>
#include <Preferences.h> 

TFT_eSPI tft = TFT_eSPI();
Preferences prefs;

// --- НАСТРОЙКИ ЖЕЛЕЗА ---
#define BUTTON_PIN 0      
#define RGB_LED_PIN 48    
#define TEST_WAVE_PIN 43  // Пин тестового генератора ШИМ

// --- ДОСТУПНЫЕ АНАЛОГОВЫЕ ПИНЫ ---
const int adcPins[] = {4, 5, 9}; 
const int NUM_ADC_PINS = 3;

// --- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ГРАФИКА ---
float voltage = 0.0;
int xPos = 0;
int prevX = 0;
int prevY = 0;
int prevAdcValue = 0;

// Аналитика сигнала
int currentMinAdc = 4095;
int currentMaxAdc = 0;
int lastMinAdc = 0;
int lastMaxAdc = 4095;
int crossings = 0;
unsigned long sweepStartTime = 0;
float freqHz = 0.0;
float vMax = 0.0;
float vMin = 0.0;
bool isFrozen = false; 

// Пользовательские настройки (сохраняются)
int currentPinIndex; 
int graphSpeed;
int smoothLevel;     
int zoomIndex;       
int graphColorIndex;
bool useGrid;
int ledMode;         
int ledColorIndex;   
int testFreqIndex;   // Индекс частоты генератора

// Списки параметров для меню и генератора
uint16_t graphColors[] = {TFT_GREEN, TFT_YELLOW, TFT_CYAN, TFT_WHITE};
const int zoomLevels[] = {1, 2, 4, 8};
const char* colorNames[] = {"GREEN", "YELLOW", "CYAN", "WHITE"};
const char* smoothNames[] = {"OFF", "LOW (5x)", "MED (20x)", "HIGH (50x)"};
const char* ledModes[] = {"OFF", "STATIC", "RAINBOW", "SWEEP"};
const char* ledColorNames[] = {"WHITE", "RED", "GREEN", "BLUE", "YELLOW", "CYAN", "MAGENTA"};

// Настройки частот генератора
const int testFreqs[] = {10, 50, 100, 500, 1000, 5000, 10000, 50000};
const int NUM_FREQS = 8;

// --- ПЕРЕМЕННЫЕ РАДУГИ ---
uint8_t rainbowHue = 0;
unsigned long lastRainbowUpdate = 0;

// --- СОСТОЯНИЯ МЕНЮ ---
// 0 = График, 1 = Меню, 2 = Настройка Генератора
int currentMode = 0; 
int menuCursor = 0;
const int MENU_ITEMS_COUNT = 10; // Пунктов стало 10

// --- ТРИГГЕРЫ КНОПКИ ---
bool isPressing = false;
unsigned long buttonPressTime = 0;
bool longPressedTriggered = false;

// Прототипы функций
void loadSettings();
void saveSettings();
void handleButton();
void drawOscilloscope();
void drawGrid();
void updateLED();
void ledWheel(uint8_t wheelPos, uint8_t &r, uint8_t &g, uint8_t &b);
void onLongPressAction();
void onShortPressAction();
void updateMenuDisplay();
void drawGenBox();

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  loadSettings();
  
  // Запускаем тестовый генератор "на постоянке"
  tone(TEST_WAVE_PIN, testFreqs[testFreqIndex]); 
  
  tft.init();
  tft.setRotation(1); 
  tft.fillScreen(TFT_BLACK);
  
  prevY = tft.height() / 2;
  sweepStartTime = millis();
  updateLED(); 
}

void loop() {
  handleButton(); 

  // График рисуется в режимах 0 (Обычный) и 2 (Настройка генератора)
  if (currentMode == 0 || currentMode == 2) {
    if (!isFrozen || currentMode == 2) {
      drawOscilloscope();
    }
    
    if (ledMode == 2 && millis() - lastRainbowUpdate >= 20) {
      lastRainbowUpdate = millis();
      rainbowHue += 2;
      updateLED();
    }
    
    if (!isFrozen) {
      delay(graphSpeed); 
    } else {
      delay(20); 
    }
  } else if (currentMode == 1) { // Меню
    if (ledMode == 2 && millis() - lastRainbowUpdate >= 20) {
      lastRainbowUpdate = millis();
      rainbowHue += 2;
      updateLED();
    }
    delay(10); 
  }
}

// ==========================================
// ПАМЯТЬ PREFERENCES
// ==========================================
void loadSettings() {
  prefs.begin("oscin", true); 
  currentPinIndex = prefs.getInt("pin_idx", 0); 
  if (currentPinIndex >= NUM_ADC_PINS) currentPinIndex = 0; 
  
  graphSpeed = prefs.getInt("speed", 10);
  smoothLevel = prefs.getInt("smooth_l", 2); 
  if (smoothLevel > 3) smoothLevel = 0;
  
  zoomIndex = prefs.getInt("zoom_idx", 0); 
  if (zoomIndex > 3) zoomIndex = 0;

  graphColorIndex = prefs.getInt("gcolor", 0);
  useGrid = prefs.getBool("grid", true);
  ledMode = prefs.getInt("lmode", 2);          
  ledColorIndex = prefs.getInt("lcolor", 0); 

  testFreqIndex = prefs.getInt("gen_idx", 4); // По умолчанию 1000 Гц (индекс 4)
  if (testFreqIndex >= NUM_FREQS) testFreqIndex = 4;
  
  prefs.end();
}

void saveSettings() {
  prefs.begin("oscin", false); 
  prefs.putInt("pin_idx", currentPinIndex);
  prefs.putInt("speed", graphSpeed);
  prefs.putInt("smooth_l", smoothLevel);
  prefs.putInt("zoom_idx", zoomIndex);
  prefs.putInt("gcolor", graphColorIndex);
  prefs.putBool("grid", useGrid);
  prefs.putInt("lmode", ledMode);
  prefs.putInt("lcolor", ledColorIndex);
  prefs.putInt("gen_idx", testFreqIndex);
  prefs.end();
}

// ==========================================
// ОТРИСОВКА ПАНЕЛИ ГЕНЕРАТОРА
// ==========================================
void drawGenBox() {
  tft.fillRect(tft.width() - 85, 8, 85, 16, TFT_BLACK);
  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  int f = testFreqs[testFreqIndex];
  char buf[20];
  if (f >= 1000) {
    snprintf(buf, sizeof(buf), "GEN:%dkHz", f / 1000);
  } else {
    snprintf(buf, sizeof(buf), "GEN:%dHz", f);
  }
  tft.drawString(buf, tft.width() - 80, 8, 2);
}

// ==========================================
// УПРАВЛЕНИЕ СВЕТОДИОДОМ
// ==========================================
void updateLED() {
  uint8_t r = 0, g = 0, b = 0;
  if (ledMode == 0) {
    neopixelWrite(RGB_LED_PIN, 0, 0, 0);
    return;
  } 
  else if (ledMode == 1) {
    switch (ledColorIndex) {
      case 0: r = 150; g = 150; b = 150; break; 
      case 1: r = 200; g = 0;   b = 0;   break; 
      case 2: r = 0;   g = 200; b = 0;   break; 
      case 3: r = 0;   g = 0;   b = 200; break; 
      case 4: r = 180; g = 180; b = 0;   break; 
      case 5: r = 0;   g = 180; b = 180; break; 
      case 6: r = 180; g = 0;   b = 180; break; 
    }
    neopixelWrite(RGB_LED_PIN, r, g, b);
  } 
  else if (ledMode == 2) {
    ledWheel(rainbowHue, r, g, b);
    neopixelWrite(RGB_LED_PIN, r, g, b);
  }
}

void ledWheel(uint8_t wheelPos, uint8_t &r, uint8_t &g, uint8_t &b) {
  wheelPos = 255 - wheelPos;
  if (wheelPos < 85) { r = 255 - wheelPos * 3; g = 0; b = wheelPos * 3; } 
  else if (wheelPos < 170) { wheelPos -= 85; r = 0; g = wheelPos * 3; b = 255 - wheelPos * 3; } 
  else { wheelPos -= 170; r = wheelPos * 3; g = 255 - wheelPos * 3; b = 0; }
  r /= 4; g /= 4; b /= 4;
}

// ==========================================
// ЛОГИКА ГРАФИКА
// ==========================================
void drawGrid() {
  int yTop = 32;
  int yBottom = tft.height() - 1;
  int h = yBottom - yTop;
  uint16_t gridColor = tft.color565(45, 45, 45); 
  for (int i = 1; i < 4; i++) tft.drawFastHLine(0, yTop + (h * i) / 4, tft.width(), gridColor);
  for (int i = 1; i < 6; i++) tft.drawFastVLine((tft.width() * i) / 6, yTop, h, gridColor);
}

void drawOscilloscope() {
  int adcValue = 0;
  int activePin = adcPins[currentPinIndex]; 

  int samples = 1;
  if (smoothLevel == 1) samples = 5;
  else if (smoothLevel == 2) samples = 20;
  else if (smoothLevel == 3) samples = 50;

  if (samples > 1) {
    long adcTotal = 0;
    for (int i = 0; i < samples; i++) {
      adcTotal += analogRead(activePin);
    }
    adcValue = adcTotal / samples;
  } else {
    adcValue = analogRead(activePin);
  }

  if (adcValue > currentMaxAdc) currentMaxAdc = adcValue;
  if (adcValue < currentMinAdc) currentMinAdc = adcValue;

  int midpoint = (lastMinAdc + lastMaxAdc) / 2;
  if (xPos > 0 && prevAdcValue < midpoint && adcValue >= midpoint) {
    crossings++;
  }
  prevAdcValue = adcValue;

  voltage = (adcValue * 3.3) / 4095.0; 

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  char topText[64];
  
  if (freqHz < 1000.0) {
    snprintf(topText, sizeof(topText), "G%d V:%.1f| ^%.1f _%.1f| %dHz  ", activePin, voltage, vMax, vMin, (int)freqHz);
  } else {
    snprintf(topText, sizeof(topText), "G%d V:%.1f| ^%.1f _%.1f| %.1fkHz  ", activePin, voltage, vMax, vMin, freqHz / 1000.0);
  }
  tft.drawString(topText, 4, 8, 2); 

  int yTop = 32; 
  int yBottom = tft.height() - 1; 
  
  int zoomedAdc = adcValue * zoomLevels[zoomIndex];
  if (zoomedAdc > 4095) zoomedAdc = 4095; 

  int yPos = map(zoomedAdc, 0, 4095, yBottom, yTop); 

  if (xPos == 0) {
    unsigned long sweepDuration = millis() - sweepStartTime;
    if (sweepDuration > 0 && (lastMaxAdc - lastMinAdc) > 100) { 
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

    prevX = 0; prevY = yPos;
    
    tft.fillRect(0, yTop, tft.width(), tft.height() - yTop, TFT_BLACK); 
    if (useGrid) drawGrid();
    if (ledMode == 3) neopixelWrite(RGB_LED_PIN, random(0, 60), random(0, 60), random(0, 60));

    // Обновляем плашку в углу
    if (currentMode == 2) {
      drawGenBox();
    } else if (isFrozen) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("HOLD", tft.width() - 40, 8, 2);
    }
  }

  tft.drawLine(prevX, prevY, xPos, yPos, graphColors[graphColorIndex]);
  
  prevX = xPos; prevY = yPos; xPos++;
  if (xPos >= tft.width()) xPos = 0; 
}

// ==========================================
// УПРАВЛЕНИЕ КНОПКОЙ (3 РЕЖИМА)
// ==========================================
void handleButton() {
  bool btnState = digitalRead(BUTTON_PIN);

  if (btnState == LOW) {
    if (!isPressing) {
      buttonPressTime = millis();
      isPressing = true;
      longPressedTriggered = false;
      delay(20); 
    } else if (!longPressedTriggered && (millis() - buttonPressTime >= 600)) {
      longPressedTriggered = true;
      onLongPressAction(); 
    }
  } else {
    if (isPressing) {
      if (!longPressedTriggered && (millis() - buttonPressTime >= 30)) {
        onShortPressAction(); 
      }
      isPressing = false;
      delay(20); 
    }
  }
}

void onLongPressAction() {
  if (currentMode == 0) {
    isFrozen = false; 
    currentMode = 1; 
    menuCursor = 0; 
    updateMenuDisplay();
  } 
  else if (currentMode == 2) {
    // ВЫХОД ИЗ РЕЖИМА ГЕНЕРАТОРА
    currentMode = 0; 
    saveSettings();
    tft.fillRect(tft.width() - 85, 8, 85, 16, TFT_BLACK); // Затираем надпись GEN:
  }
  else if (currentMode == 1) { // Внутри меню
    switch (menuCursor) {
      case 0: currentPinIndex = (currentPinIndex + 1) % NUM_ADC_PINS; break;
      case 1: graphSpeed += 10; if (graphSpeed > 50) graphSpeed = 0; break;
      case 2: smoothLevel = (smoothLevel + 1) % 4; break;
      case 3: zoomIndex = (zoomIndex + 1) % 4; break;
      case 4: graphColorIndex = (graphColorIndex + 1) % 4; break;
      case 5: useGrid = !useGrid; break;
      case 6: ledMode = (ledMode + 1) % 4; updateLED(); break;
      case 7: ledColorIndex = (ledColorIndex + 1) % 7; updateLED(); break;
      case 8: // ВХОД В НАСТРОЙКУ ГЕНЕРАТОРА
        currentMode = 2;
        isFrozen = false; // Снимаем паузу принудительно
        xPos = 0;
        tft.fillScreen(TFT_BLACK);
        drawGenBox();
        return; 
      case 9: // ВЫХОД ИЗ МЕНЮ
        saveSettings(); 
        currentMode = 0;
        xPos = 0; 
        tft.fillScreen(TFT_BLACK);
        return;
    }
    updateMenuDisplay(); 
  }
}

void onShortPressAction() {
  if (currentMode == 0) {
    // ПАУЗА (HOLD) ТОЛЬКО В РЕЖИМЕ 0
    isFrozen = !isFrozen; 
    if (isFrozen) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("HOLD", tft.width() - 40, 8, 2);
    } else {
      tft.fillRect(tft.width() - 40, 8, 40, 16, TFT_BLACK);
    }
  } 
  else if (currentMode == 2) {
    // ПЕРЕКЛЮЧЕНИЕ ЧАСТОТЫ ГЕНЕРАТОРА
    testFreqIndex = (testFreqIndex + 1) % NUM_FREQS;
    tone(TEST_WAVE_PIN, testFreqs[testFreqIndex]); // Меняем частоту ШИМ на лету
    drawGenBox(); // Мгновенно обновляем плашку на экране
  }
  else if (currentMode == 1) {
    // ЛИСТАНИЕ МЕНЮ
    menuCursor = (menuCursor + 1) % MENU_ITEMS_COUNT; 
    updateMenuDisplay();
  }
}

// ==========================================
// ИНТЕРФЕЙС МЕНЮ
// ==========================================
void updateMenuDisplay() {
  tft.fillScreen(TFT_BLACK);
  
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("     OSCILLOSCOPE MENU", 10, 2, 2);
  tft.drawFastHLine(0, 18, tft.width(), TFT_YELLOW);

  char buf[40];
  int yStart = 20;  
  int yStep = 14;  // Слегка уплотнил строки, чтобы влезло всё

  tft.setTextColor(menuCursor == 0 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
  snprintf(buf, sizeof(buf), "%s Channel: GPIO %d", (menuCursor == 0 ? ">" : " "), adcPins[currentPinIndex]);
  tft.drawString(buf, 10, yStart + (0 * yStep), 2);

  tft.setTextColor(menuCursor == 1 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
  snprintf(buf, sizeof(buf), "%s Sweep Speed: %d ms", (menuCursor == 1 ? ">" : " "), graphSpeed);
  tft.drawString(buf, 10, yStart + (1 * yStep), 2);

  tft.setTextColor(menuCursor == 2 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
  snprintf(buf, sizeof(buf), "%s Filter: %s", (menuCursor == 2 ? ">" : " "), smoothNames[smoothLevel]);
  tft.drawString(buf, 10, yStart + (2 * yStep), 2);

  tft.setTextColor(menuCursor == 3 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
  snprintf(buf, sizeof(buf), "%s Y-Axis Zoom: %dx", (menuCursor == 3 ? ">" : " "), zoomLevels[zoomIndex]);
  tft.drawString(buf, 10, yStart + (3 * yStep), 2);

  tft.setTextColor(menuCursor == 4 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
  snprintf(buf, sizeof(buf), "%s Scope Color: %s", (menuCursor == 4 ? ">" : " "), colorNames[graphColorIndex]);
  tft.drawString(buf, 10, yStart + (4 * yStep), 2);

  tft.setTextColor(menuCursor == 5 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
  snprintf(buf, sizeof(buf), "%s Grid Bg: %s", (menuCursor == 5 ? ">" : " "), useGrid ? "ON" : "OFF");
  tft.drawString(buf, 10, yStart + (5 * yStep), 2);

  tft.setTextColor(menuCursor == 6 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
  snprintf(buf, sizeof(buf), "%s RGB Mode: %s", (menuCursor == 6 ? ">" : " "), ledModes[ledMode]);
  tft.drawString(buf, 10, yStart + (6 * yStep), 2);

  tft.setTextColor(menuCursor == 7 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
  snprintf(buf, sizeof(buf), "%s RGB Color: %s", (menuCursor == 7 ? ">" : " "), ledColorNames[ledColorIndex]);
  tft.drawString(buf, 10, yStart + (7 * yStep), 2);

  // НОВЫЙ ПУНКТ: Настройка генератора
  tft.setTextColor(menuCursor == 8 ? TFT_MAGENTA : TFT_WHITE, TFT_BLACK);
  snprintf(buf, sizeof(buf), "%s [ TUNE TEST GENERATOR ]", (menuCursor == 8 ? ">" : " "));
  tft.drawString(buf, 10, yStart + (8 * yStep), 2);

  tft.setTextColor(menuCursor == 9 ? TFT_RED : TFT_CYAN, TFT_BLACK);
  snprintf(buf, sizeof(buf), "%s [ SAVE & BACK TO GRAPH ]", (menuCursor == 9 ? ">" : " "));
  tft.drawString(buf, 10, yStart + (9 * yStep) + 2, 2);
}

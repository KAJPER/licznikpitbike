// Minimalny program: TFT_eSPI z jawnie podanymi wymiarami (320x240) + rotacja 1
// Odniesienia: Arduino Forum
// - https://forum.arduino.cc/t/esp32-and-2-4-tft-unable-to-use-the-full-size-of-the-display/1184915
// - https://forum.arduino.cc/t/adafruit-ili9341-not-using-the-whole-display/964672/27

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <FS.h>
// Możemy też użyć JPG; na razie używamy XBM i BMP z SPIFFS
// Nowocześniejszy UI z gradientowym paskiem RPM, znacznikami oraz inną czcionką
// GFX FreeFonts są opcjonalne (-DLOAD_GFXFF=1). Dodatkowo obsłużymy Smooth Font z plików .vlw.
// Czcionki GFX są opcjonalne; włączone przez -DLOAD_GFXFF
#if defined(__has_include)
  #if __has_include(<Fonts/FreeSansBold12pt7b.h>)
    #include <Fonts/FreeSansBold12pt7b.h>
    #define HAS_FSB12 1
  #else
    #define HAS_FSB12 0
  #endif
  #if __has_include(<Fonts/FreeSansBold24pt7b.h>)
    #include <Fonts/FreeSansBold24pt7b.h>
    #define HAS_FSB24 1
  #else
    #define HAS_FSB24 0
  #endif
#else
  #define HAS_FSB12 0
  #define HAS_FSB24 0
#endif

// Kluczowa zmiana: jawne wymiary panelu w konstruktorze
TFT_eSPI tft = TFT_eSPI(320, 240);

// Dane licznika (testowo symulowane). Docelowo możesz podmienić na realne pomiary
static uint16_t currentRpm = 1200;   // 0..16000
static uint16_t currentSpeed = 0;    // km/h
static int8_t currentGear = 0;       // 0 = luz, 1..6

// Skala RPM
static const uint16_t RPM_MAX = 16000;
static const uint8_t  RPM_TICKS = 17; // co 1000 rpm (0..16k)

// Obszary rysowania (ekran 320x240 przy setRotation(0))
struct Rect { int16_t x, y, w, h; };
static const Rect AREA_RPM   = { 16, 18, 288, 36 };   // górny pasek RPM
static const Rect AREA_SPEED = { 40, 72, 180, 110 };  // centralna prędkość
static const Rect AREA_GEAR  = { 238, 72, 70, 110 };  // prawy bieg
static const Rect AREA_LABEL = { 12, 200, 296, 28 };  // dolne etykiety

// Smooth Font (VLW) – ścieżki w SPIFFS: wgraj pliki do folderu data/ i użyj PlatformIO: Upload Filesystem
static bool smoothFontsReady = false;
// Użyj dwóch rozmiarów z folderu data/: Final-Frontier48 i Final-Frontier24
static const char* FONT_SPEED_VLW = "/Final-Frontier48.vlw"; // duża czcionka prędkości i biegu
static const char* FONT_LABEL_VLW = "/Final-Frontier24.vlw"; // etykiety

// LED sygnalizacyjny – użyjemy kanału B z RGB (IO16), aktywnie niski
#define LED_B_PIN 16
static inline void ledBlue(bool on) { digitalWrite(LED_B_PIN, on ? LOW : HIGH); }

// Wejścia: RPM i biegi
// RPM – wejście impulsów z cewki zapłonowej
#define PIN_RPM     21
// Czujniki biegów (aktywnie niskie: zwierają do masy)
#define PIN_1_BIEG  32
#define PIN_N_BIEG  26
#define PIN_2_BIEG  25
#define PIN_3_BIEG  5
#define PIN_4_BIEG  4
#define PIN_5_BIEG  17

volatile uint32_t rpmPulseCount = 0;
volatile uint32_t rpmLastIrqMs = 0;
static const uint32_t RPM_IRQ_DEBOUNCE_MS = 2; // filtr zakłóceń z cewki

static void IRAM_ATTR rpmIsr() {
  uint32_t now = millis();
  if (now - rpmLastIrqMs >= RPM_IRQ_DEBOUNCE_MS) {
    rpmPulseCount++;
    rpmLastIrqMs = now;
  }
}

// --------------------------- Splash screen (XBM logo + progress) ---------------------------
// Prosty 1-bit XBM (32x32) – ikona koła zębatych
static const uint8_t SPLASH_W = 32;
static const uint8_t SPLASH_H = 32;
static const unsigned char splash_xbm[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x80,0x01,0x80,0x01,
  0x40,0x02,0x40,0x02,0x20,0x04,0x20,0x04,
  0x10,0x08,0x10,0x08,0x90,0x09,0x90,0x09,
  0xd0,0x0b,0xd0,0x0b,0xf0,0x0f,0xf0,0x0f,
  0xf0,0x0f,0xf0,0x0f,0xd0,0x0b,0xd0,0x0b,
  0x90,0x09,0x90,0x09,0x10,0x08,0x10,0x08,
  0x20,0x04,0x20,0x04,0x40,0x02,0x40,0x02,
  0x80,0x01,0x80,0x01,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

static void listSpiffs() {
  if (!SPIFFS.begin(true)) { Serial.println("[SPIFFS] begin failed"); return; }
  Serial.println("[SPIFFS] files:");
  fs::File root = SPIFFS.open("/");
  fs::File f = root.openNextFile();
  while (f) {
    Serial.printf("  %s (%u bytes)\n", f.name(), (unsigned)f.size());
    f = root.openNextFile();
  }
}

static const bool SPLASH_ROTATE_180 = true;

static void showSplashScreen() {
  bool bgDrawn = false;
  // Tło: wczytaj /splash.bmp (24-bit, bez kompresji)
  if (SPIFFS.begin(true) && SPIFFS.exists("/splash.bmp")) {
    fs::File f = SPIFFS.open("/splash.bmp", "r");
    if (f) {
      uint16_t bfType = f.read() | (f.read() << 8);
      if (bfType == 0x4D42) { // 'BM'
        f.seek(10); uint32_t offset = f.read() | (f.read()<<8) | (f.read()<<16) | (f.read()<<24);
        f.seek(18); int32_t w = f.read() | (f.read()<<8) | (f.read()<<16) | (f.read()<<24);
        int32_t h = f.read() | (f.read()<<8) | (f.read()<<16) | (f.read()<<24);
        f.seek(28); uint16_t bpp = f.read() | (f.read()<<8);
        if (bpp != 24) { Serial.printf("[BMP] Unsupported bpp=%u (only 24)\n", bpp); f.close(); }
        else {
        f.seek(offset);
        uint32_t rowSize = ((24 * w + 31) / 32) * 4;
        std::unique_ptr<uint8_t[]> row(new uint8_t[rowSize]);
        int startX = max(0, (tft.width() - (int)w) / 2);
        int startY = max(0, (tft.height() - (int)h) / 2);
        for (int y = h - 1; y >= 0; y--) {
          f.read(row.get(), rowSize);
          int drawY = SPLASH_ROTATE_180 ? (startY + y) : (startY + (h-1 - y));
          if (drawY < 0 || drawY >= tft.height()) continue;
          for (int x = 0; x < w; x++) {
            int drawX = SPLASH_ROTATE_180 ? (startX + (w - 1 - x)) : (startX + x);
            if (drawX < 0 || drawX >= tft.width()) continue;
            // BMP przechowuje piksele w kolejności BGR – zamieniamy na 565
            uint8_t b = row[x*3 + 0];
            uint8_t g = row[x*3 + 1];
            uint8_t r = row[x*3 + 2];
            tft.drawPixel(drawX, drawY, ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
          }
        }
        f.close();
        bgDrawn = true;
        Serial.printf("[BMP] Drawn splash %dx%d at (%d,%d)\n", (int)w, (int)h, startX, startY);
        }
      } else {
        f.close();
      }
    }
  }
  if (!bgDrawn) {
    tft.fillScreen(TFT_BLACK);
  }

  // Pasek postępu na dole (bez napisów/ikony)
  // Napis w centrum podczas ładowania
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_BLUE);
  if (smoothFontsReady) {
    tft.loadFont(FONT_LABEL_VLW);
    tft.drawString("MADE BY KAJPA", tft.width()/2, tft.height()/2);
    tft.unloadFont();
  } else {
    tft.drawString("MADE BY KAJPA", tft.width()/2, tft.height()/2, 4);
  }

  // Pasek postępu na dole
  int bx = 20, by = tft.height() - 26, bw = tft.width() - 40, bh = 10;
  tft.drawRoundRect(bx, by, bw, bh, 4, TFT_BLUE);
  for (int p = 0; p <= 100; p += 4) {
    int w = (bw - 2) * p / 100;
    uint16_t c = tft.color565(30 + (p*2), 120, 160);
    tft.fillRoundRect(bx + 1, by + 1, w, bh - 2, 3, c);
    delay(12);
  }
}

// Dotyk rezystancyjny 4-przewodowy: X+, X-, Y+, Y-
// Podłącz: Y+->IO32 (ADC1), X+->IO33 (ADC1), X-->IO25, Y-->IO26
#define RES_YP 32
#define RES_XP 33
#define RES_XM 25
#define RES_YM 26

// Dolny panel: ODOMETER/TRIP/MOTO HOURS + logika potrójnego tapnięcia
enum BottomMode { MODE_ODOM, MODE_TRIP, MODE_HOURS };
static BottomMode bottomMode = MODE_ODOM;
static float odomKm = 0.0f;   // całkowity przebieg [km]
static float tripKm = 0.0f;   // przebieg dzienny [km]
static float motoHours = 0.0f;// motogodziny [h]
static uint32_t lastIntegrateMs = 0;
// Single-tap switch (debounce) + filtry i autokalibracja
static uint32_t lastSwitchMs = 0;
static const uint32_t TOUCH_SWITCH_DEBOUNCE_MS = 300;
static int touchEmaY = 0, touchEmaX = 0;
static bool isPressed = false;
static bool touchCalibrated = false;
static uint32_t touchCalibStartMs = 0;
static const uint32_t TOUCH_CALIB_MS = 1200;
static int baseY = 0, baseX = 0;
static int thrPressY = 0, thrReleaseY = 0;
static int deltaMarginX = 250;
static const bool TOUCH_DEBUG = true;

static void drawBottomPanel();

static void drawLabels() {
  // Czyścimy dolny pasek etykiet i rysujemy tylko podpis dla biegu
  tft.fillRect(AREA_LABEL.x, AREA_LABEL.y, AREA_LABEL.w, AREA_LABEL.h, TFT_BLACK);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  #if HAS_FSB12
    tft.setFreeFont(&FreeSansBold12pt7b);
  #endif
  tft.drawString("BIEG", AREA_GEAR.x, AREA_GEAR.y - 8);
  drawBottomPanel();
}

static void drawRpmTrack() {
  // Tło toru RPM
  tft.fillRoundRect(AREA_RPM.x, AREA_RPM.y, AREA_RPM.w, AREA_RPM.h, 6, 0x0841); // ciemny granat
  // Gradient bazowy toru
  #ifdef TFT_ESPI_ENABLE_HSPI // bez znaczenia; tylko, by uniknąć ostrzeżeń
  #endif
  // Użyj horyzontalnego gradientu, jeśli dostępny; jeśli nie, degradacja do kilku pasów
  #ifdef TFT_ESPI_ENABLEDMA
  #endif
  // Spróbuj wypełnienia segmentami, by uzyskać efekt gradientu
  int segs = 24;
  int sw = AREA_RPM.w / segs;
  for (int i = 0; i < segs; ++i) {
    uint8_t r = (uint8_t)map(i, 0, segs - 1, 0, 255);
    uint8_t g = (uint8_t)map(i, 0, segs - 1, 180, 0);
    uint8_t b = (uint8_t)map(i, 0, segs - 1, 255, 0);
    uint16_t col = tft.color565(r, g, b);
    tft.fillRoundRect(AREA_RPM.x + i * sw, AREA_RPM.y, sw + 1, AREA_RPM.h, 6, col);
  }

  // Kreski (ticki) co 1000 rpm
  int x0 = AREA_RPM.x;
  for (uint8_t t = 0; t < RPM_TICKS; ++t) {
    int x = x0 + (int)((t * 1000.0f / RPM_MAX) * AREA_RPM.w);
    int h = (t % 2 == 0) ? AREA_RPM.h : AREA_RPM.h * 3 / 4;
    uint16_t c = (t >= 14) ? TFT_RED : TFT_BLACK; // czerwone w czerwonej strefie
    tft.drawFastVLine(x, AREA_RPM.y + (AREA_RPM.h - h) / 2, h, c);
  }
}

static void drawStaticUi() {
  tft.fillScreen(TFT_BLACK);
  drawLabels();
}

static void updateRpm(uint16_t rpm) {
  // Minimal: tylko jedna linia wycentrowana "<wartosc> RPM"
  tft.fillRect(AREA_RPM.x, AREA_RPM.y, AREA_RPM.w, AREA_RPM.h, TFT_BLACK);

  char buf[24];
  snprintf(buf, sizeof(buf), "%u RPM", rpm);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  if (smoothFontsReady) {
    tft.loadFont(FONT_LABEL_VLW); // ok. 24 px
    tft.drawString(buf, AREA_RPM.x + AREA_RPM.w / 2, AREA_RPM.y + AREA_RPM.h / 2 + 1);
    tft.unloadFont();
  } else {
    #if HAS_FSB12
      tft.setFreeFont(&FreeSansBold12pt7b);
      tft.drawString(buf, AREA_RPM.x + AREA_RPM.w / 2, AREA_RPM.y + AREA_RPM.h / 2 + 1);
    #else
      tft.drawString(buf, AREA_RPM.x + AREA_RPM.w / 2, AREA_RPM.y + AREA_RPM.h / 2, 4);
    #endif
  }
}

static void updateSpeed(uint16_t kmh) {
  // Czyść obszar prędkości
  tft.fillRect(AREA_SPEED.x, AREA_SPEED.y, AREA_SPEED.w, AREA_SPEED.h, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  // Preferuj Smooth Font dla nowoczesnego wyglądu
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", kmh);
  if (smoothFontsReady) {
    tft.loadFont(FONT_SPEED_VLW);
    tft.drawString(buf, AREA_SPEED.x + AREA_SPEED.w / 2, AREA_SPEED.y + AREA_SPEED.h / 2 - 2);
    tft.unloadFont();
  } else {
    tft.drawString(buf, AREA_SPEED.x + AREA_SPEED.w / 2, AREA_SPEED.y + AREA_SPEED.h / 2 - 10, 8);
  }
  #if HAS_FSB12
    tft.setFreeFont(&FreeSansBold12pt7b);
  #endif
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("km/h", AREA_SPEED.x + AREA_SPEED.w / 2, AREA_SPEED.y + AREA_SPEED.h - 8);
  drawBottomPanel();
}

static void updateGear(int8_t gear) {
  // Czyść obszar biegu
  tft.fillRect(AREA_GEAR.x, AREA_GEAR.y, AREA_GEAR.w, AREA_GEAR.h, TFT_BLACK);
  tft.drawRoundRect(AREA_GEAR.x, AREA_GEAR.y, AREA_GEAR.w, AREA_GEAR.h, 8, TFT_DARKGREY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(gear == 0 ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
  const char* g = "N";
  char num[2] = {0};
  if (gear == 0) {
    g = "N";
  } else if (gear > 0 && gear <= 9) {
    num[0] = '0' + gear; g = num;
  }
  if (smoothFontsReady) {
    tft.loadFont(FONT_SPEED_VLW);
    tft.drawString(g, AREA_GEAR.x + AREA_GEAR.w / 2, AREA_GEAR.y + AREA_GEAR.h / 2 + 6);
    tft.unloadFont();
  } else {
    #if HAS_FSB24
      tft.setFreeFont(&FreeSansBold24pt7b);
      tft.drawString(g, AREA_GEAR.x + AREA_GEAR.w / 2, AREA_GEAR.y + AREA_GEAR.h / 2 + 6);
    #else
      tft.drawString(g, AREA_GEAR.x + AREA_GEAR.w / 2, AREA_GEAR.y + AREA_GEAR.h / 2 + 6, 8);
    #endif
  }
  drawBottomPanel();
}

void setup() {
  Serial.begin(115200);

#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif

  tft.init();
  tft.setRotation(0);      // zawsze 0 (zgodnie z prośbą)
  tft.invertDisplay(false);

  // Splash screen
  showSplashScreen();
  listSpiffs();

  // Inicjalizacja SPIFFS dla Smooth Font
  if (SPIFFS.begin(true)) {
    if (SPIFFS.exists(FONT_SPEED_VLW) && SPIFFS.exists(FONT_LABEL_VLW)) {
      smoothFontsReady = true;
    }
  }

  // Wejście RPM i biegi
  pinMode(PIN_RPM, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_RPM), rpmIsr, FALLING);
  pinMode(PIN_1_BIEG, INPUT_PULLUP);
  pinMode(PIN_N_BIEG, INPUT_PULLUP);
  pinMode(PIN_2_BIEG, INPUT_PULLUP);
  pinMode(PIN_3_BIEG, INPUT_PULLUP);
  pinMode(PIN_4_BIEG, INPUT_PULLUP);
  pinMode(PIN_5_BIEG, INPUT_PULLUP);
  pinMode(LED_B_PIN, OUTPUT); digitalWrite(LED_B_PIN, HIGH);

  // Dotyk rezystancyjny – ustaw spoczynkowo wejścia
  pinMode(RES_XP, INPUT);
  pinMode(RES_XM, INPUT);
  pinMode(RES_YP, INPUT);
  pinMode(RES_YM, INPUT);
  touchCalibrated = false;
  touchCalibStartMs = millis();

  drawStaticUi();
  updateRpm(currentRpm);
  updateSpeed(currentSpeed);
  updateGear(currentGear);
}

void loop() {
  // Symulacja zmian (do testów UI). Podmień na realne odczyty z czujników.
  static uint32_t last = 0;
  if (millis() - last > 200) {
    last = millis();

    // Realne RPM z liczby impulsów: 4T -> RPM = impulsy * 120 (imp/s * 60 * 2)
    static uint32_t lastRpmCalcMs = millis();
    uint32_t nowMs = millis();
    uint32_t dtMs = nowMs - lastRpmCalcMs;
    if (dtMs >= 250) { // okno 0.25 s
      noInterrupts();
      uint32_t pulses = rpmPulseCount; rpmPulseCount = 0;
      interrupts();
      float pulsesPerSec = (dtMs > 0) ? (pulses * 1000.0f / dtMs) : 0.0f;
      float rpmf = pulsesPerSec * 120.0f; // dla 4-suwa
      if (rpmf < 0) rpmf = 0; if (rpmf > 20000) rpmf = 20000;
      currentRpm = (uint16_t)rpmf;
      lastRpmCalcMs = nowMs;
    }

    // Bieg – odczyt aktywnego GND na wejściach (N=0, 1..5)
    int8_t gear = -1;
    if (digitalRead(PIN_N_BIEG) == LOW) gear = 0;
    else if (digitalRead(PIN_1_BIEG) == LOW) gear = 1;
    else if (digitalRead(PIN_2_BIEG) == LOW) gear = 2;
    else if (digitalRead(PIN_3_BIEG) == LOW) gear = 3;
    else if (digitalRead(PIN_4_BIEG) == LOW) gear = 4;
    else if (digitalRead(PIN_5_BIEG) == LOW) gear = 5;
    if (gear >= 0) currentGear = gear;

    // Na razie prędkość stała 0 (do czasu podłączenia Halla)
    currentSpeed = 0;

    // (opcjonalnie) sygnał zmiany biegu – na razie wyłączony

    // Sygnał zmiany biegu przy 6000 RPM – miganie diodą LED (IO16, aktywnie LOW)
    static bool shiftFlashOn = false;
    static bool wasShiftActive = false;
    const uint16_t SHIFT_RPM = 6000;
    bool shiftActive = (currentRpm >= SHIFT_RPM && currentRpm < 10000);
    if (shiftActive) {
      shiftFlashOn = !shiftFlashOn;
      ledBlue(shiftFlashOn);
    } else if (wasShiftActive) {
      ledBlue(false);
      shiftFlashOn = false;
    }
    wasShiftActive = shiftActive;

    // Miganie całego ekranu na czerwono przy wysokich RPM
    static bool flashOn = false;         // stan klatki (czerwony/ekran UI)
    static bool wasFlashActive = false;  // czy poprzednio był aktywny alert
    const uint16_t THRESH = 10000;       // próg odcinki – dopasuj

    bool flashActive = (currentRpm >= THRESH);
    if (flashActive) {
      flashOn = !flashOn;
      if (flashOn) {
        tft.fillScreen(TFT_BLUE);
      } else {
        drawStaticUi();
        updateSpeed(currentSpeed);
        updateGear(currentGear);
        updateRpm(currentRpm);
      }
    } else if (wasFlashActive) {
      // Schodzimy z odcinki – przywróć pełny UI
      drawStaticUi();
      updateSpeed(currentSpeed);
      updateGear(currentGear);
      updateRpm(currentRpm);
      flashOn = false;
    }
    wasFlashActive = flashActive;

    updateRpm(currentRpm);
    updateSpeed(currentSpeed);
    updateGear(currentGear);
  }

  // Detekcja pojedynczego tapnięcia – rezystancyjny
  static uint32_t lastTouchPoll = 0;
  if (millis() - lastTouchPoll > 25) {
    lastTouchPoll = millis();
    // Zasil X, czytaj Y+
    pinMode(RES_XP, OUTPUT); digitalWrite(RES_XP, HIGH);
    pinMode(RES_XM, OUTPUT); digitalWrite(RES_XM, LOW);
    pinMode(RES_YP, INPUT);
    int rawY = analogRead(RES_YP);
    // Zasil Y, czytaj X+
    pinMode(RES_YP, OUTPUT); digitalWrite(RES_YP, HIGH);
    pinMode(RES_YM, OUTPUT); digitalWrite(RES_YM, LOW);
    pinMode(RES_XP, INPUT);
    int rawX = analogRead(RES_XP);

    if (TOUCH_DEBUG && (millis() % 250 < 25)) {
      Serial.printf("[TOUCH] rawY=%d rawX=%d\n", rawY, rawX);
    }

    touchEmaY = (int)(0.7f * touchEmaY + 0.3f * rawY);
    touchEmaX = (int)(0.7f * touchEmaX + 0.3f * rawX);

    if (!touchCalibrated && millis() - touchCalibStartMs >= TOUCH_CALIB_MS) {
      baseY = touchEmaY; baseX = touchEmaX;
      thrPressY = baseY + 250; thrReleaseY = baseY + 120;
      touchCalibrated = true;
      Serial.printf("[TOUCH] Calibrated baseY=%d baseX=%d thrP=%d thrR=%d\n", baseY, baseX, thrPressY, thrReleaseY);
    }

    bool active = (touchCalibrated && ((touchEmaY >= thrPressY) || (touchEmaX >= baseX + deltaMarginX)));
    if (!isPressed && active) {
      isPressed = true;
      uint32_t now = millis();
      if (now - lastSwitchMs > TOUCH_SWITCH_DEBOUNCE_MS) {
        bottomMode = (BottomMode)(((int)bottomMode + 1) % 3);
        drawBottomPanel();
        Serial.println("[TOUCH] Single-tap -> switch panel");
        lastSwitchMs = now;
      }
    } else if (isPressed && (touchEmaY <= thrReleaseY) && (touchEmaX <= baseX + (deltaMarginX/2))) {
      isPressed = false;
    }
  }
}

static void drawBottomPanel() {
  // Integrowanie dystansu i motogodzin
  uint32_t now = millis();
  if (lastIntegrateMs == 0) lastIntegrateMs = now;
  uint32_t dt = now - lastIntegrateMs;
  if (dt > 0) {
    // dystans [km] = (km/h) * (dt[h])
    float d_km = (float)currentSpeed * (dt / 3600000.0f);
    odomKm += d_km;
    tripKm += d_km;
    // motogodziny: licz tylko gdy RPM > 0 (symulacja)
    if (currentRpm > 0) motoHours += dt / 3600000.0f;
    lastIntegrateMs = now;
  }

  // Render dolnego paska
  tft.fillRect(AREA_LABEL.x, AREA_LABEL.y, AREA_LABEL.w, AREA_LABEL.h, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  if (smoothFontsReady) {
    tft.loadFont(FONT_LABEL_VLW);
  } else {
    #if HAS_FSB12
      tft.setFreeFont(&FreeSansBold12pt7b);
    #endif
  }

  char line[32];
  switch (bottomMode) {
    case MODE_ODOM:
      snprintf(line, sizeof(line), "ODO %.1f km", odomKm);
      break;
    case MODE_TRIP:
      snprintf(line, sizeof(line), "TRIP %.1f km", tripKm);
      break;
    case MODE_HOURS:
      snprintf(line, sizeof(line), "MOTO %.1f h", motoHours);
      break;
  }
  tft.drawString(line, AREA_LABEL.x + AREA_LABEL.w / 2, AREA_LABEL.y + AREA_LABEL.h / 2);

  if (smoothFontsReady) tft.unloadFont();
}
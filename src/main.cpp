// Minimalny program: TFT_eSPI z jawnie podanymi wymiarami (320x240) + rotacja 1
// Odniesienia: Arduino Forum
// - https://forum.arduino.cc/t/esp32-and-2-4-tft-unable-to-use-the-full-size-of-the-display/1184915
// - https://forum.arduino.cc/t/adafruit-ili9341-not-using-the-whole-display/964672/27

#include <Arduino.h>
#include <TFT_eSPI.h>
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

static void drawLabels() {
  // Czyścimy dolny pasek etykiet i rysujemy tylko podpis dla biegu
  tft.fillRect(AREA_LABEL.x, AREA_LABEL.y, AREA_LABEL.w, AREA_LABEL.h, TFT_BLACK);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  #if HAS_FSB12
    tft.setFreeFont(&FreeSansBold12pt7b);
  #endif
  tft.drawString("BIEG", AREA_GEAR.x, AREA_GEAR.y - 8);
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

  // Inicjalizacja SPIFFS dla Smooth Font
  if (SPIFFS.begin(true)) {
    if (SPIFFS.exists(FONT_SPEED_VLW) && SPIFFS.exists(FONT_LABEL_VLW)) {
      smoothFontsReady = true;
    }
  }

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

    currentRpm += 350; if (currentRpm > 14500) currentRpm = 900;
    currentSpeed += 3; if (currentSpeed > 180) currentSpeed = 0;
    if (currentSpeed == 0) currentGear = 0; else currentGear = 1 + ((currentSpeed / 25) % 6);

    // Miganie całego ekranu na czerwono przy wysokich RPM
    static bool flashOn = false;         // stan klatki (czerwony/ekran UI)
    static bool wasFlashActive = false;  // czy poprzednio był aktywny alert
    const uint16_t THRESH = 10000;       // próg odcinki – dopasuj

    bool flashActive = (currentRpm >= THRESH);
    if (flashActive) {
      flashOn = !flashOn;
      if (flashOn) {
        tft.fillScreen(TFT_RED);
      } else {
        drawStaticUi();
        updateSpeed(currentSpeed);
        updateGear(currentGear);
      }
    } else if (wasFlashActive) {
      // Schodzimy z odcinki – przywróć pełny UI
      drawStaticUi();
      updateSpeed(currentSpeed);
      updateGear(currentGear);
      flashOn = false;
    }
    wasFlashActive = flashActive;

    updateRpm(currentRpm);
    updateSpeed(currentSpeed);
    updateGear(currentGear);
  }
}
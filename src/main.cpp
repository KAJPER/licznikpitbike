// Minimalny program: TFT_eSPI z jawnie podanymi wymiarami (320x240) + rotacja 1
// Odniesienia: Arduino Forum
// - https://forum.arduino.cc/t/esp32-and-2-4-tft-unable-to-use-the-full-size-of-the-display/1184915
// - https://forum.arduino.cc/t/adafruit-ili9341-not-using-the-whole-display/964672/27

#include <Arduino.h>
#include <TFT_eSPI.h>

// Kluczowa zmiana: jawne wymiary panelu w konstruktorze
TFT_eSPI tft = TFT_eSPI(320, 240);

// Dane licznika (testowo symulowane). Docelowo możesz podmienić na realne pomiary
static uint16_t currentRpm = 1200;   // 0..16000
static uint16_t currentSpeed = 0;    // km/h
static int8_t currentGear = 0;       // 0 = luz, 1..6

// Obszary rysowania (do czyszczenia tylko fragmentów ekranu)
struct Rect { int16_t x, y, w, h; };
static const Rect AREA_SPEED = { 40, 80, 160, 90 };   // centralna duża prędkość
static const Rect AREA_RPM   = { 10, 10, 300, 40 };   // pasek górny z RPM
static const Rect AREA_GEAR  = { 230, 70, 80, 100 };  // prawa duża cyfra biegu

static void drawStaticUi() {
  tft.fillScreen(TFT_BLACK);

  // Nagłówki
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("RPM", AREA_RPM.x, AREA_RPM.y - 12, 2);
  tft.drawString("km/h", AREA_SPEED.x, AREA_SPEED.y - 14, 2);
  tft.drawString("GEAR", AREA_GEAR.x, AREA_GEAR.y - 14, 2);
}

static void updateRpm(uint16_t rpm) {
  // Czyść obszar wartości RPM
  tft.fillRect(AREA_RPM.x, AREA_RPM.y, AREA_RPM.w, AREA_RPM.h, TFT_BLACK);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  char buf[16];
  snprintf(buf, sizeof(buf), "%5u", rpm);
  tft.drawString(buf, AREA_RPM.x, AREA_RPM.y + AREA_RPM.h / 2, 4);
}

static void updateSpeed(uint16_t kmh) {
  // Czyść obszar prędkości
  tft.fillRect(AREA_SPEED.x, AREA_SPEED.y, AREA_SPEED.w, AREA_SPEED.h, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", kmh);
  // Duża czcionka (Font 8)
  tft.drawString(buf, AREA_SPEED.x + AREA_SPEED.w / 2, AREA_SPEED.y + AREA_SPEED.h / 2, 8);
}

static void updateGear(int8_t gear) {
  // Czyść obszar biegu
  tft.fillRect(AREA_GEAR.x, AREA_GEAR.y, AREA_GEAR.w, AREA_GEAR.h, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  const char* g = "N";
  char num[2] = {0};
  if (gear == 0) {
    g = "N"; // luz
  } else if (gear > 0 && gear <= 9) {
    num[0] = '0' + gear; g = num;
  }
  tft.drawString(g, AREA_GEAR.x + AREA_GEAR.w / 2, AREA_GEAR.y + AREA_GEAR.h / 2, 8);
}

void setup() {
  Serial.begin(115200);

#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif

  tft.init();
  tft.setRotation(0);      // poziomo (landscape)
  tft.invertDisplay(false);

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

    updateRpm(currentRpm);
    updateSpeed(currentSpeed);
    updateGear(currentGear);
  }
}
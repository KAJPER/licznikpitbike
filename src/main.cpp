#include <TFT_eSPI.h>

// Wyświetlacz (TFT_eSPI konfigurowane w User_Setup.h / platformio.ini)
TFT_eSPI tft = TFT_eSPI();

// Maska na dolny pas ekranu z artefaktami (nie rysujemy tam nic)
static const int MASK_BOTTOM = 40; // px – stałe odcięcie

// Piny dla prostego statusu LED (opcjonalnie)
#define LED_R 17
#define LED_G 4
#define LED_B 16

#ifndef TFT_BACKLIGHT_ON
#define TFT_BACKLIGHT_ON HIGH
#endif

// Zmienne stanu „silnika” (symulacja)
float rpm = 1500.0f;
float speedKmh = 0.0f;
int currentGear = 1;

// Poprzednie wartości do selektywnego odświeżania
float lastRpmDrawn = -1.0f;
float lastSpeedDrawn = -1.0f;
int lastGearDrawn = -1;

// Rozmiary „widocznej” części (po odjęciu maski)
inline int visibleWidth() { return tft.width(); }
inline int visibleHeight() { return tft.height() - MASK_BOTTOM; }
inline int safeX(int x) { return constrain(x, 0, visibleWidth() - 1); }
inline int safeY(int y) { return constrain(y, 0, visibleHeight() - 1); }

// Prototypy
void initDisplayLandscape();
void maskBottomBand();
void drawDashboardFrame();
void updateRpmBar(float valueRpm);
void updateSpeed(float valueKmh);
void updateGear(int gear);
void updateRpmText(float valueRpm);
void simulateEngineStep();

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  digitalWrite(LED_R, HIGH);
  digitalWrite(LED_G, HIGH);
  digitalWrite(LED_B, HIGH);

#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
#endif

  tft.init();
  initDisplayLandscape();

  // Rysunek stałych elementów UI
  drawDashboardFrame();

  // Startowe odczyty
  updateRpmBar(rpm);
  updateRpmText(rpm);
  updateSpeed(speedKmh);
  updateGear(currentGear);
}

void loop() {
  static unsigned long lastUpdate = 0;
  unsigned long now = millis();
  if (now - lastUpdate >= 50) { // 20 FPS
    simulateEngineStep();

    updateRpmBar(rpm);
    updateRpmText(rpm);
    updateSpeed(speedKmh);
    updateGear(currentGear);

    lastUpdate = now;
  }

  // Okresowo odśwież maskę dolnego pasa, gdyby pojawiły się przebitki
  static unsigned long lastMask = 0;
  if (now - lastMask > 500) {
    maskBottomBand();
    lastMask = now;
  }
}

// === Implementacja ===

void initDisplayLandscape() {
  // Pozioma orientacja; zamieniam na 3, aby tekst był w poziomie zgodnie z montażem
  tft.setRotation(3);

  // Rysujemy zawsze w bezpiecznym obszarze (viewport)
  tft.setViewport(0, 0, visibleWidth(), visibleHeight(), true);
  tft.fillScreen(TFT_BLACK);

  // Zamaluj nieużywany dolny pas
  maskBottomBand();
}

void maskBottomBand() {
  // Wyłącz viewport na chwilę, zamaluj dolny pas, przywróć viewport
  tft.resetViewport();
  tft.fillRect(0, tft.height() - MASK_BOTTOM, tft.width(), MASK_BOTTOM, TFT_BLACK);
  tft.setViewport(0, 0, visibleWidth(), visibleHeight(), true);
}

void drawDashboardFrame() {
  const int w = visibleWidth();
  const int h = visibleHeight();

  tft.fillScreen(TFT_BLACK);

  // Obwódka całego widocznego obszaru
  tft.drawRect(0, 0, w, h, TFT_DARKGREY);

  // Pasek RPM – po samej górze
  const int rpmX = 16;
  const int rpmY = 12;
  const int rpmW = w - rpmX * 2;
  const int rpmH = 18;
  tft.drawRect(rpmX - 1, rpmY - 1, rpmW + 2, rpmH + 2, TFT_WHITE);

  // Prostokąt prędkości (tylko delikatne prowadnice)
  const int speedBoxW = w - 60;
  const int speedBoxH = 64;
  const int speedBoxX = (w - speedBoxW) / 2;
  const int speedBoxY = h / 2 - speedBoxH / 2;
  tft.drawRoundRect(speedBoxX, speedBoxY, speedBoxW, speedBoxH, 8, TFT_DARKGREY);

  // Okno biegu w prawym dolnym rogu (nad maską)
  const int gearW = 64;
  const int gearH = 48;
  const int gearX = w - gearW - 12;
  const int gearY = h - gearH - 10;
  tft.drawRoundRect(gearX, gearY, gearW, gearH, 6, TFT_YELLOW);
}

void updateRpmBar(float valueRpm) {
  if (fabs(valueRpm - lastRpmDrawn) < 50.0f) return; // unikaj zbędnych przerysowań

  const int w = visibleWidth();
  const int rpmX = 16;
  const int rpmY = 12;
  const int rpmW = w - rpmX * 2;
  const int rpmH = 18;

  // Tło paska
  tft.fillRect(rpmX, rpmY, rpmW, rpmH, TFT_BLACK);

  // Procent paska (0..12000 rpm)
  float frac = constrain(valueRpm / 12000.0f, 0.0f, 1.0f);
  int pw = (int)(frac * rpmW);

  uint16_t color = TFT_GREEN;
  if (valueRpm > 10000) color = TFT_YELLOW;
  if (valueRpm > 11500) color = TFT_RED;
  if (pw > 0) tft.fillRect(rpmX, rpmY, pw, rpmH, color);

  lastRpmDrawn = valueRpm;
}

void updateSpeed(float valueKmh) {
  if (fabs(valueKmh - lastSpeedDrawn) < 1.0f) return;

  const int w = visibleWidth();
  const int h = visibleHeight();
  const int speedBoxW = w - 60;
  const int speedBoxH = 64;
  const int speedBoxX = (w - speedBoxW) / 2;
  const int speedBoxY = h / 2 - speedBoxH / 2;

  // Czyść wnętrze boksu
  tft.fillRect(speedBoxX + 2, speedBoxY + 2, speedBoxW - 4, speedBoxH - 4, TFT_BLACK);

  // Duża wartość prędkości
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(4);
  char buf[8];
  snprintf(buf, sizeof(buf), "%3.0f", valueKmh);
  // Prosty centrowany zapis
  int textW = strlen(buf) * 24; // przy size=4 ~24 px/znak
  int sx = speedBoxX + (speedBoxW - textW) / 2;
  int sy = speedBoxY + (speedBoxH - 32) / 2; // ~32 px wysokość linii
  tft.setCursor(safeX(sx), safeY(sy));
  tft.print(buf);

  // Jednostka
  tft.setTextSize(2);
  tft.setCursor(safeX(sx + textW + 6), safeY(sy + 20));
  tft.print("km/h");

  lastSpeedDrawn = valueKmh;
}

void updateGear(int gear) {
  if (gear == lastGearDrawn) return;

  const int w = visibleWidth();
  const int h = visibleHeight();
  const int gearW = 64;
  const int gearH = 48;
  const int gearX = w - gearW - 12;
  const int gearY = h - gearH - 10;

  // Czyść wnętrze
  tft.fillRect(gearX + 2, gearY + 2, gearW - 4, gearH - 4, TFT_BLACK);

  // Rysuj cyfrę biegu
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  char gb[4];
  snprintf(gb, sizeof(gb), "%d", gear);
  int tw = strlen(gb) * 18; // ~18 px / znak dla size=3
  int gx = gearX + (gearW - tw) / 2;
  int gy = gearY + (gearH - 24) / 2;
  tft.setCursor(safeX(gx), safeY(gy));
  tft.print(gb);

  lastGearDrawn = gear;
}

void updateRpmText(float valueRpm) {
  static float lastRpmText = -1.0f;
  if (fabs(valueRpm - lastRpmText) < 100.0f) return;

  const int w = visibleWidth();
  // Mały tekst w prawym górnym rogu
  const int tx = w - 110;
  const int ty = 36;

  tft.fillRect(tx, ty, 108, 16, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  char rb[16];
  snprintf(rb, sizeof(rb), "%.0f RPM", valueRpm);
  tft.setCursor(safeX(tx), safeY(ty));
  tft.print(rb);

  lastRpmText = valueRpm;
}

// Prosta symulacja pracy – naprzemienne przyspieszanie/hamowanie i zmiany biegów
void simulateEngineStep() {
  static unsigned long lastStep = 0;
  unsigned long now = millis();
  float dt = (now - lastStep) / 1000.0f;
  if (dt < 0.05f) return; // ~20 Hz

  static bool accel = true;

  // Progi zmiany biegów
  const int maxGear = 5;
  const float upShift[maxGear]   = { 9500, 10500, 11000, 11500, 12000 };
  const float downShift[maxGear] = {  800,  3000,  4000,  4500,  5000 };

  // Zmieniaj „gaz” pomiędzy zakresami
  if (rpm >= 11800 && accel) { accel = false; }
  if (rpm <= 1500  && !accel) { accel = true;  }

  float a = accel ? 180.0f : -220.0f; // rpm/s
  rpm = constrain(rpm + a * dt, 800.0f, 12000.0f);

  // Zmiany biegów
  if (currentGear < maxGear && rpm > upShift[currentGear - 1]) {
    currentGear++;
    rpm *= 0.7f;
  } else if (currentGear > 1 && rpm < downShift[currentGear - 1]) {
    currentGear--;
    rpm *= 1.25f;
  }

  // Prędkość – prosty model: vmax rośnie z biegiem
  const float vmaxForGear[maxGear] = { 40, 60, 70, 90, 107 };
  float vmax = vmaxForGear[currentGear - 1];
  speedKmh = constrain((rpm / 12000.0f) * vmax, 0.0f, vmax);

  lastStep = now;
}


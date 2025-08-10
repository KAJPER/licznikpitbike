// Minimalny program: TFT_eSPI z jawnie podanymi wymiarami (320x240) + rotacja 1
// Odniesienia: Arduino Forum
// - https://forum.arduino.cc/t/esp32-and-2-4-tft-unable-to-use-the-full-size-of-the-display/1184915
// - https://forum.arduino.cc/t/adafruit-ili9341-not-using-the-whole-display/964672/27

#include <Arduino.h>
#include <TFT_eSPI.h>

// Kluczowa zmiana: jawne wymiary panelu w konstruktorze
TFT_eSPI tft = TFT_eSPI(320, 240);

void setup() {
  Serial.begin(115200);

#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif

  tft.init();
  tft.setRotation(0);      // tryb poziomy; z (320,240) rozwiązuje problem „uciętego” obszaru
  tft.invertDisplay(false);
  tft.fillScreen(TFT_BLACK);

  // Obramowanie pomocnicze do weryfikacji pełnego obszaru
  tft.drawRect(0, 0, tft.width(), tft.height(), TFT_DARKGREY);

  // Napis na środku ekranu
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("test", tft.width() / 2, tft.height() / 2, 4);
}

void loop() {
}
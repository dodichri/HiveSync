#include "display.h"

#include <SPI.h>
#include <qrcode_st7789.h>
#include <Fonts/FreeSans12pt7b.h>

#ifndef TFT_CS
#error "Display requires a board variant defining TFT pins (TFT_CS/DC/RST/BACKLITE)."
#endif

static Adafruit_ST7789 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
static QRcode_ST7789 qrcode(&tft);

void display_powerOn() {
#ifdef TFT_I2C_POWER
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
#endif
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);
}

void display_backlight(bool on) {
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, on ? HIGH : LOW);
}

void display_init() {
  display_powerOn();
  SPI.begin(SCK, MISO, MOSI, SS);
  tft.init(135, 240);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);
  tft.setFont(&FreeSans12pt7b);
}

void display_fillScreen(uint16_t color) {
  tft.fillScreen(color);
}

void display_fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  tft.fillRect(x, y, w, h, color);
}

void display_printAt(const String &text, int16_t y, uint16_t color) {
  tft.setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  int16_t x = -x1; // align left edge at x=0
  if (x < 0) x = 0;
  tft.setCursor(x, y);
  tft.print(text);
}

void display_showQR(const String &payload) {
  qrcode.init();
  qrcode.create(payload.c_str());
}

void display_showIP(const IPAddress &ip) {
  tft.fillScreen(ST77XX_BLACK);
  display_printAt("HiveSync", TFT_LINE_1, COLOR_HIVE_YELLOW);
  display_printAt(ip.toString(), TFT_LINE_2, COLOR_SIGNAL_BLUE);
}

void display_showSensorsAndSleep(float tempC, const char* weightLine) {
  tft.fillScreen(ST77XX_BLACK);
  display_printAt("HiveSync", TFT_LINE_1, COLOR_HIVE_YELLOW);
  char buf[32];
  snprintf(buf, sizeof(buf), "Temp: %.2f C", tempC);
  display_printAt(String(buf), TFT_LINE_2, COLOR_WHITE_SMOKE);
  display_printAt(String(weightLine), TFT_LINE_3, COLOR_WHITE_SMOKE);
  display_printAt("Sleeping 15 min...", TFT_LINE_4, COLOR_SIGNAL_BLUE);
}

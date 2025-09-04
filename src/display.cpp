#include "display.h"

#include <SPI.h>
#include <qrcode_st7789.h>
#include <Fonts/FreeSans12pt7b.h>
#include "battery.h"

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

void display_drawBatteryTopRight() {
  float pct = 0.0f, volt = 0.0f;
  if (!battery_read(pct, volt)) {
    return; // no device detected; skip overlay
  }

  char buf[24];
  // Compact format to fit alongside title
  // Example: 87% 4.09V
  snprintf(buf, sizeof(buf), "%.0f%% %.2fV", pct, volt);

  // Measure text bounds for right alignment at screen width 240
  const int16_t baselineY = TFT_LINE_1;
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(buf, 0, baselineY, &x1, &y1, &w, &h);
  int16_t targetX = 240 - (int16_t)w - 4; // right margin
  // Compute actual bounding box at the draw position to clear background
  int16_t bx, by; uint16_t bw, bh;
  tft.getTextBounds(buf, targetX, baselineY, &bx, &by, &bw, &bh);
  // Clear just behind the overlay to keep left-side content intact
  tft.fillRect(bx - 1, by - 1, bw + 2, bh + 2, ST77XX_BLACK);

  // Color by level for quick glance
  uint16_t color = ST77XX_GREEN;
  if (pct <= 20.0f) color = ST77XX_RED;
  else if (pct <= 40.0f) color = ST77XX_YELLOW;

  tft.setTextColor(color);
  tft.setCursor(targetX, baselineY);
  tft.print(buf);
}

void display_showQR(const String &payload) {
  qrcode.init();
  qrcode.create(payload.c_str());
  display_drawBatteryTopRight();
}

void display_showIP(const IPAddress &ip) {
  tft.fillScreen(ST77XX_BLACK);
  display_printAt("HiveSync", TFT_LINE_1, ST77XX_YELLOW);
  display_printAt(ip.toString(), TFT_LINE_2, ST77XX_CYAN);
  display_drawBatteryTopRight();
}

void display_showSensorsAndSleep(float tempC, const char* weightLine) {
  tft.fillScreen(ST77XX_BLACK);
  display_printAt("HiveSync", TFT_LINE_1, ST77XX_YELLOW);
  char buf[32];
  snprintf(buf, sizeof(buf), "Temp: %.2f C", tempC);
  display_printAt(String(buf), TFT_LINE_2, ST77XX_WHITE);
  display_printAt(String(weightLine), TFT_LINE_3, ST77XX_WHITE);
  display_printAt("Sleeping 15 min...", TFT_LINE_4, ST77XX_CYAN);
  display_drawBatteryTopRight();
}


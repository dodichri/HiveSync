#include "display.h"

#include <SPI.h>
#include <qrcode_st7789.h>
#include <Fonts/FreeSans12pt7b.h>
#include "battery.h"
#include "theme.h"

#ifndef TFT_CS
#error "Display requires a board variant defining TFT pins (TFT_CS/DC/RST/BACKLITE)."
#endif

static Adafruit_ST7789 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
static QRcode_ST7789 qrcode(&tft);

// Draw a simple WiFi icon composed of three arcs + dot.
// Positioned relative to left margin with the dot aligned near the provided baseline.
static void drawWifiIconAt(int16_t xLeft, int16_t baselineY, uint16_t color) {
  // Center x of the icon; place a small left margin from xLeft
  int16_t cx = xLeft + 12;     // total width ~24px
  int16_t cy = baselineY - 2;  // arcs center slightly above the dot

  // Clear a small background behind icon for contrast (black)
  int16_t left = cx - 13;
  int16_t top  = cy - 13;
  int16_t w    = 26;
  int16_t h    = 18;
  tft.fillRect(left, top, w, h, ST77XX_BLACK);

  // Draw three semi-circles by drawing full circles then masking lower half with black
  // Outer arc (thickness ~2px)
  tft.drawCircle(cx, cy, 12, color);
  tft.drawCircle(cx, cy, 11, color);
  // Middle arc
  tft.drawCircle(cx, cy, 8, color);
  tft.drawCircle(cx, cy, 7, color);
  // Inner arc
  tft.drawCircle(cx, cy, 4, color);
  tft.drawCircle(cx, cy, 3, color);

  // Mask bottom half so only the top arcs remain
  tft.fillRect(left, cy + 1, w, h, ST77XX_BLACK);

  // Dot below arcs
  tft.fillCircle(cx, baselineY + 2, 2, color);
}

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
  // Show only battery percentage, e.g., "87%"
  snprintf(buf, sizeof(buf), "%.0f%%", pct);

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
  uint16_t color = ST77XX_GREEN;               // high
  if (pct <= 20.0f) color = ST77XX_RED;        // low
  else if (pct <= 40.0f) color = ST77XX_YELLOW;// medium

  tft.setTextColor(color);
  tft.setCursor(targetX, baselineY);
  tft.print(buf);
}

void display_drawBatteryAndWifiTopRight(uint16_t wifiColor) {
  const int16_t baselineY = TFT_LINE_1;
  float pct = 0.0f, volt = 0.0f;

  // If no battery, still draw WiFi icon anchored to right margin
  if (!battery_read(pct, volt)) {
    const int16_t iconWidth = 24;
    int16_t iconLeft = 240 - iconWidth - 4; // right margin
    drawWifiIconAt(iconLeft, baselineY, wifiColor);
    return;
  }

  char buf[24];
  snprintf(buf, sizeof(buf), "%.0f%%", pct);

  // Measure text bounds to right-align at screen width 240
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(buf, 0, baselineY, &x1, &y1, &w, &h);
  int16_t targetX = 240 - (int16_t)w - 4; // right margin

  // Clear background just behind the percentage text
  int16_t bx, by; uint16_t bw, bh;
  tft.getTextBounds(buf, targetX, baselineY, &bx, &by, &bw, &bh);
  tft.fillRect(bx - 1, by - 1, bw + 2, bh + 2, ST77XX_BLACK);

  // Draw WiFi icon immediately to the left of the battery percentage
  const int16_t iconWidth = 24;
  int16_t iconLeft = targetX - iconWidth - 4; // 4px gap
  drawWifiIconAt(iconLeft, baselineY, wifiColor);

  // Colorize battery text by level
  uint16_t color = ST77XX_GREEN;               // high
  if (pct <= 20.0f) color = ST77XX_RED;        // low
  else if (pct <= 40.0f) color = ST77XX_YELLOW;// medium

  tft.setTextColor(color);
  tft.setCursor(targetX, baselineY);
  tft.print(buf);
}

void display_showQR(const String &payload) {
  qrcode.init();
  qrcode.create(payload.c_str());
  // Not connected: show WiFi icon next to battery in white smoke
  display_drawBatteryAndWifiTopRight(COLOR_WHITE_SMOKE);
}

void display_showIP(const IPAddress &ip) {
  tft.fillScreen(ST77XX_BLACK);
  display_printAt("HiveSync", TFT_LINE_1, THEME_TEXT_BRAND);
  // Connected: show WiFi icon next to battery in signal blue
  display_drawBatteryAndWifiTopRight(COLOR_SIGNAL_BLUE);
}

void display_showSensorsAndSleep(float tempC, const char* weightLine) {
  tft.fillScreen(ST77XX_BLACK);
  display_printAt("HiveSync", TFT_LINE_1, THEME_TEXT_BRAND);
  char buf[32];
  snprintf(buf, sizeof(buf), "Temp: %.2f C", tempC);
  display_printAt(String(buf), TFT_LINE_2, THEME_TEXT_PRIMARY);
  display_printAt(String(weightLine), TFT_LINE_3, THEME_TEXT_PRIMARY);
  display_printAt("Sleeping 15 min...", TFT_LINE_4, THEME_TEXT_ACCENT);
  display_drawBatteryTopRight();
}

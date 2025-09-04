// Display utilities for Adafruit ST7789 + QR rendering
#pragma once

#include <Arduino.h>
#include <WiFi.h> // for IPAddress
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// Common Y positions used across screens (kept from original layout)
#define TFT_LINE_1  18
#define TFT_LINE_2  42
#define TFT_LINE_3  66
#define TFT_LINE_4  90
#define TFT_LINE_5  114

// Initialize power, SPI, TFT, and font
void display_init();

// Power control
void display_powerOn();
void display_backlight(bool on);

// Simple wrappers
void display_fillScreen(uint16_t color);
void display_fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void display_printAt(const String &text, int16_t y, uint16_t color = ST77XX_WHITE);

// Composed views
void display_showQR(const String &payload);
void display_showIP(const IPAddress &ip);
void display_showSensorsAndSleep(float tempC, const char* weightLine);

// Overlay: draw battery percentage at top-right of line 1
void display_drawBatteryTopRight();

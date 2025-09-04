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

// Brand color palette (RGB565)
// Hive Yellow   #FFB400 -> 0xFDA0
// Deep Teal     #007C91 -> 0x03F2
// White Smoke   #F5F5F5 -> 0xF7BE
// Signal Blue   #4A90E2 -> 0x4C9C
#define COLOR_HIVE_YELLOW  0xFDA0
#define COLOR_DEEP_TEAL    0x03F2
#define COLOR_WHITE_SMOKE  0xF7BE
#define COLOR_SIGNAL_BLUE  0x4C9C

// Initialize power, SPI, TFT, and font
void display_init();

// Power control
void display_powerOn();
void display_backlight(bool on);

// Simple wrappers
void display_fillScreen(uint16_t color);
void display_fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void display_printAt(const String &text, int16_t y, uint16_t color = COLOR_WHITE_SMOKE);

// Composed views
void display_showQR(const String &payload);
void display_showIP(const IPAddress &ip);
void display_showSensorsAndSleep(float tempC, const char* weightLine);

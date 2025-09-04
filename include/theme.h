// Theme palette for HiveSync TFT UI (16-bit RGB565)
// Allowed colors only: Hive Yellow, Deep Teal, White Smoke, Signal Blue

#pragma once

// Precomputed RGB565 values for the brand palette
// Hive Yellow #FFB400 -> rgb565 0xFDA0
#define COLOR_HIVE_YELLOW 0xFDA0
// Deep Teal #007C91 -> rgb565 0x03F2
#define COLOR_DEEP_TEAL   0x03F2
// White Smoke #F5F5F5 -> rgb565 0xF7BE
#define COLOR_WHITE_SMOKE 0xF7BE
// Signal Blue #4A90E2 -> rgb565 0x4C9C
#define COLOR_SIGNAL_BLUE 0x4C9C

// Role-based aliases for consistency across screens
#define THEME_TEXT_PRIMARY   COLOR_WHITE_SMOKE
#define THEME_TEXT_BRAND     COLOR_HIVE_YELLOW
#define THEME_TEXT_ACCENT    COLOR_DEEP_TEAL
// No red available in palette; use brand yellow for emphasis/errors
#define THEME_TEXT_ERROR     COLOR_HIVE_YELLOW


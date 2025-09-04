// Battery monitor utilities using Adafruit MAX17048
#pragma once

#include <Arduino.h>

// Initialize I2C + MAX17048 (safe to call even if device absent)
void battery_init();

// Read battery percent (0-100) and voltage (V). Returns false if device not found.
bool battery_read(float &percent, float &voltage);


// Unified pin configuration for HiveSync hardware
// Override any of these via PlatformIO build_flags (-DNAME=value)
#pragma once

#include <Arduino.h>

// Buttons
#ifndef BOOT_BTN_PIN
#define BOOT_BTN_PIN 0  // Boot/clear provisioning button
#endif

#ifndef CAL_BTN_PIN
  #ifdef D1
    #define CAL_BTN_PIN D1  // Preferred alias when available
  #else
    #define CAL_BTN_PIN 1   // Fallback GPIO
  #endif
#endif

#ifndef SEL_BTN_PIN
  #ifdef D2
    #define SEL_BTN_PIN D2  // Preferred alias when available
  #else
    #define SEL_BTN_PIN 2   // Fallback GPIO
  #endif
#endif

// DS18B20 (OneWire) temperature sensor
#ifndef DS18B20_PIN
#define DS18B20_PIN 9
#endif

// HX711 load cell amplifier
#ifndef HX711_DOUT_PIN
#define HX711_DOUT_PIN 10
#endif
#ifndef HX711_SCK_PIN
#define HX711_SCK_PIN 11
#endif

// INMP441 I2S microphone pins
#ifndef I2S_WS_PIN
#define I2S_WS_PIN  13   // LRCLK / WS
#endif
#ifndef I2S_SCK_PIN
#define I2S_SCK_PIN 12   // BCLK / SCK
#endif
#ifndef I2S_SD_PIN
#define I2S_SD_PIN  14   // DOUT from INMP441 -> SD
#endif

// Note: TFT pins (TFT_CS/TFT_DC/TFT_RST/TFT_BACKLITE) are provided by the
// selected board variant (e.g., Adafruit Feather ESP32-S3 Reverse TFT).
// Define/override them here via build flags only if your board variant
// does not set them.


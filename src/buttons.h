// Button pin defaults and helpers
#pragma once

#include <Arduino.h>

// Pins (can be overridden via build flags)
#ifndef BOOT_BTN_PIN
#define BOOT_BTN_PIN 0  // D0: boot/clear provisioning
#endif

#ifndef CAL_BTN_PIN
  #ifdef D1
    #define CAL_BTN_PIN D1  // Use D1 for calibration
  #else
    #define CAL_BTN_PIN 1   // fallback GPIO1
  #endif
#endif

#ifndef SEL_BTN_PIN
  #ifdef D2
    #define SEL_BTN_PIN D2
  #else
    #define SEL_BTN_PIN 2   // fallback GPIO2
  #endif
#endif

// Electrical config
#ifndef BOOT_BTN_ACTIVE_LEVEL
#define BOOT_BTN_ACTIVE_LEVEL LOW
#endif
#ifndef BOOT_BTN_INPUT_MODE
#define BOOT_BTN_INPUT_MODE INPUT_PULLUP
#endif

#ifndef CAL_BTN_ACTIVE_LEVEL
#define CAL_BTN_ACTIVE_LEVEL HIGH   // D1 uses pulldown; pressed ties HIGH
#endif
#ifndef CAL_BTN_INPUT_MODE
#define CAL_BTN_INPUT_MODE INPUT_PULLDOWN
#endif

#ifndef SEL_BTN_ACTIVE_LEVEL
#define SEL_BTN_ACTIVE_LEVEL HIGH   // D2 uses pulldown; pressed ties HIGH
#endif
#ifndef SEL_BTN_INPUT_MODE
#define SEL_BTN_INPUT_MODE INPUT_PULLDOWN
#endif

// Setup all button pin modes once
void buttons_setupPins();

// Helpers
bool buttons_pressed(uint8_t pin, int activeLevel);
void buttons_waitRelease(uint8_t pin, int activeLevel);
void buttons_waitPress(uint8_t pin, int activeLevel);
uint32_t buttons_measureHoldMs(uint8_t pin, uint32_t max_ms, uint8_t inputMode, int activeLevel);


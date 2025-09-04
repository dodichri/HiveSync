// Button pin defaults and helpers
#pragma once

#include <Arduino.h>
#include "pins_config.h"

// Pins are centralized in pins_config.h

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

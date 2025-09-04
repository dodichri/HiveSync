#include "buttons.h"

void buttons_setupPins() {
  pinMode(BOOT_BTN_PIN, BOOT_BTN_INPUT_MODE);
  pinMode(CAL_BTN_PIN, CAL_BTN_INPUT_MODE);
  pinMode(SEL_BTN_PIN, SEL_BTN_INPUT_MODE);
}

bool buttons_pressed(uint8_t pin, int activeLevel) {
  return digitalRead(pin) == activeLevel;
}

void buttons_waitRelease(uint8_t pin, int activeLevel) {
  while (buttons_pressed(pin, activeLevel)) delay(10);
}

void buttons_waitPress(uint8_t pin, int activeLevel) {
  while (!buttons_pressed(pin, activeLevel)) delay(10);
}

uint32_t buttons_measureHoldMs(uint8_t pin, uint32_t max_ms, uint8_t inputMode, int activeLevel) {
  pinMode(pin, inputMode);
  if (!buttons_pressed(pin, activeLevel)) return 0;
  uint32_t start = millis();
  while (buttons_pressed(pin, activeLevel) && (millis() - start) < max_ms) {
    delay(10);
  }
  return millis() - start;
}


#include "battery.h"

#include <Wire.h>
#include <Adafruit_MAX1704X.h>

static Adafruit_MAX17048 g_max17048;
static bool g_batt_present = false;
static bool g_batt_inited = false;

void battery_init() {
  if (g_batt_inited) return;
  g_batt_inited = true;

  // Ensure I2C is started; use defaults for this board
  Wire.begin();
  g_batt_present = g_max17048.begin(&Wire);
  if (!g_batt_present) {
    Serial.println("MAX17048 not detected on I2C (0x36). Battery overlay disabled.");
  } else {
    Serial.println("MAX17048 detected. Battery overlay enabled.");
  }
}

bool battery_read(float &percent, float &voltage) {
  if (!g_batt_inited) battery_init();
  if (!g_batt_present) return false;
  // Read state of charge and voltage
  percent = g_max17048.cellPercent();
  voltage = g_max17048.cellVoltage();
  // Basic sanity clamp
  if (!(percent >= 0.0f && percent <= 100.0f)) {
    percent = constrain(percent, 0.0f, 100.0f);
  }
  return true;
}


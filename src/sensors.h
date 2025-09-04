// Sensor utilities: DS18B20 and HX711 + calibration persistence/UI
#pragma once

#include <Arduino.h>

// DS18B20 pin (override via build flags)
#ifndef DS18B20_PIN
#define DS18B20_PIN 9
#endif

// HX711 pins (override via build flags)
#ifndef HX711_DOUT_PIN
#define HX711_DOUT_PIN 10
#endif
#ifndef HX711_SCK_PIN
#define HX711_SCK_PIN 11
#endif

// Units label default
#ifndef HX711_UNITS_LABEL
#define HX711_UNITS_LABEL "lbs"
#endif

// Initialization (pins, loading calibration)
void sensors_init();

// DS18B20
bool sensors_readDS18B20C(float &outC);

// HX711
bool sensors_readHX711(long &outRaw, bool &hasUnits, float &outUnits, int samples = 10);
bool sensors_runHX711Calibration();
void sensors_powerDown();


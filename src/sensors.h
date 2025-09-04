// Sensor utilities: DS18B20 and HX711 + calibration persistence/UI
#pragma once

#include <Arduino.h>
#include "pins_config.h"

// DS18B20 and HX711 pins are centralized in pins_config.h

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

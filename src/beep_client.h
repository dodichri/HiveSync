#pragma once

#include <Arduino.h>

// Sends current readings to BEEP API.
// Returns true on success.
bool beep_sendReadings(
    float tempC,
    bool tempOK,
    bool hxOK,
    bool hxHasUnits,
    long hxRaw,
    float hxUnits,
    bool audioOK,
    const float* audioBands,
    size_t bandsCount);


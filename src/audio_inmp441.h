// Audio capture and FFT analysis for INMP441 I2S microphone (ESP32/Arduino)
#pragma once

#include <Arduino.h>

// Configure I2S pins for INMP441 (override in platformio.ini build_flags)
#ifndef I2S_WS_PIN
#define I2S_WS_PIN  13   // LRCLK / WS
#endif
#ifndef I2S_SCK_PIN
#define I2S_SCK_PIN  12  // BCLK / SCK
#endif
#ifndef I2S_SD_PIN
#define I2S_SD_PIN   14  // DOUT from INMP441 -> SD
#endif

// Sample rate and FFT size
#ifndef I2S_SAMPLE_RATE
#define I2S_SAMPLE_RATE 16000  // Hz
#endif

#ifndef FFT_N
#define FFT_N 4096  // power-of-two, determines frequency resolution
#endif

// Number of analysis bands (fixed list below)
#define AUDIO_BANDS 10

// Perform a 60-second capture and FFT-based band aggregation.
// outBands receives average magnitude per requested band order:
//  98-146, 146-195, 195-244, 244-293, 293-342,
//  342-391, 391-439, 439-488, 488-537, 537-586
// Returns true on success; false if I2S setup fails.
bool analyzeINMP441Bins60s(float outBands[AUDIO_BANDS]);


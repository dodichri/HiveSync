#include "audio_inmp441.h"

#include <arduinoFFT.h>
#include "driver/i2s.h"
#include "esp_heap_caps.h"

static const uint16_t kBandLow[AUDIO_BANDS]  = {  98, 146, 195, 244, 293, 342, 391, 439, 488, 537 };
static const uint16_t kBandHigh[AUDIO_BANDS] = { 146, 195, 244, 293, 342, 391, 439, 488, 537, 586 };

// Local helpers
static bool i2s_setup(uint32_t sample_rate) {
  // I2S configuration for standard I2S, 32-bit samples, RX only
  i2s_config_t i2s_config = {};
  i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  i2s_config.sample_rate = sample_rate;
  i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  i2s_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT; // INMP441 with L/R tied to GND -> left channel
  i2s_config.communication_format = I2S_COMM_FORMAT_STAND_MSB;
  i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2s_config.dma_buf_count = 8;
  i2s_config.dma_buf_len = 256; // samples per DMA buffer
  i2s_config.use_apll = false;
  i2s_config.tx_desc_auto_clear = false;
  i2s_config.fixed_mclk = 0;

  i2s_pin_config_t pin_config = {};
  pin_config.bck_io_num = I2S_SCK_PIN;
  pin_config.ws_io_num = I2S_WS_PIN;
  pin_config.data_out_num = I2S_PIN_NO_CHANGE; // microphone is input-only
  pin_config.data_in_num = I2S_SD_PIN;

  if (i2s_driver_install(I2S_NUM_0, &i2s_config, 0, nullptr) != ESP_OK) {
    return false;
  }
  if (i2s_set_pin(I2S_NUM_0, &pin_config) != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_0);
    return false;
  }
  // Ensure clock is set (some IDF versions require explicit set after driver install)
  i2s_set_clk(I2S_NUM_0, sample_rate, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
  return true;
}

static void i2s_teardown() {
  i2s_driver_uninstall(I2S_NUM_0);
}

// Read exactly "count" 32-bit samples from I2S into dest. Blocks until done.
static void i2s_read_blocking(int32_t* dest, size_t count) {
  uint8_t* ptr = reinterpret_cast<uint8_t*>(dest);
  size_t bytes_to_read = count * sizeof(int32_t);
  size_t total = 0;
  while (total < bytes_to_read) {
    size_t br = 0;
    i2s_read(I2S_NUM_0, ptr + total, bytes_to_read - total, &br, portMAX_DELAY);
    total += br;
  }
}

bool analyzeINMP441Bins60s(float outBands[AUDIO_BANDS]) {
  for (int i = 0; i < AUDIO_BANDS; ++i) outBands[i] = 0.0f;

  if (!i2s_setup(I2S_SAMPLE_RATE)) {
    return false;
  }

  // Discard the first ~100ms to stabilize mic/clock
  {
    const size_t junk_samples = (I2S_SAMPLE_RATE / 10);
    int32_t* junk = (int32_t*)heap_caps_malloc(junk_samples * sizeof(int32_t), MALLOC_CAP_8BIT);
    if (junk) {
      i2s_read_blocking(junk, junk_samples);
      free(junk);
    } else {
      // Fallback: small stack buffer loop
      const size_t chunk = 256;
      int32_t buf[chunk];
      size_t remain = junk_samples;
      while (remain) {
        size_t take = remain > chunk ? chunk : remain;
        i2s_read_blocking(buf, take);
        remain -= take;
      }
    }
  }

  // FFT setup
  static_assert((FFT_N & (FFT_N - 1)) == 0, "FFT_N must be power of two");
  const double fs = (double)I2S_SAMPLE_RATE;
  const double freq_res = fs / (double)FFT_N;

  // Precompute band index ranges
  int bandStart[AUDIO_BANDS];
  int bandEnd[AUDIO_BANDS];
  for (int b = 0; b < AUDIO_BANDS; ++b) {
    int s = (int)ceil((double)kBandLow[b] / freq_res);
    int e = (int)floor((double)kBandHigh[b] / freq_res);
    if (s < 1) s = 1;                // skip DC bin
    if (e > (FFT_N / 2 - 1)) e = (FFT_N / 2 - 1);
    if (e < s) e = s;
    bandStart[b] = s;
    bandEnd[b] = e;
  }

  // Allocate FFT buffers
  double* vReal = (double*)heap_caps_malloc(sizeof(double) * FFT_N, MALLOC_CAP_8BIT);
  double* vImag = (double*)heap_caps_malloc(sizeof(double) * FFT_N, MALLOC_CAP_8BIT);
  int32_t* i2sBuf = (int32_t*)heap_caps_malloc(sizeof(int32_t) * FFT_N, MALLOC_CAP_8BIT);
  if (!vReal || !vImag || !i2sBuf) {
    if (vReal) free(vReal);
    if (vImag) free(vImag);
    if (i2sBuf) free(i2sBuf);
    i2s_teardown();
    return false;
  }
  ArduinoFFT FFT = ArduinoFFT(vReal, vImag, FFT_N, fs);

  // Capture/analyze for 60 seconds (may exceed by up to one frame)
  const uint32_t start_ms = millis();
  const uint32_t limit_ms = 60UL * 1000UL;
  uint32_t frames = 0;

  while ((millis() - start_ms) <= limit_ms) {
    // Read one FFT frame from I2S
    i2s_read_blocking(i2sBuf, FFT_N);

    // Convert and remove DC (mean)
    double mean = 0.0;
    for (int i = 0; i < FFT_N; ++i) {
      // INMP441 provides 24-bit data in 32-bit word, MSB aligned
      // Shift to get 24-bit signed sample and scale to double
      int32_t s = i2sBuf[i] >> 8; // keep top 24 bits
      vReal[i] = (double)s;
      mean += vReal[i];
      vImag[i] = 0.0;
    }
    mean /= (double)FFT_N;
    for (int i = 0; i < FFT_N; ++i) vReal[i] -= mean;

    // Windowing + FFT
    FFT.windowing(vReal, FFT_N, FFT_WIN_TYP_HANN, FFT_FORWARD);
    FFT.compute(vReal, vImag, FFT_N, FFT_FORWARD);
    FFT.complexToMagnitude(vReal, vImag, FFT_N);

    // Aggregate bands over this frame
    for (int b = 0; b < AUDIO_BANDS; ++b) {
      double sum = 0.0;
      for (int k = bandStart[b]; k <= bandEnd[b]; ++k) {
        sum += vReal[k];
      }
      outBands[b] += (float)sum;
    }
    frames++;
  }

  // Average over frames and normalize by number of bins per band
  if (frames == 0) frames = 1;
  for (int b = 0; b < AUDIO_BANDS; ++b) {
    int bins = (bandEnd[b] - bandStart[b] + 1);
    if (bins < 1) bins = 1;
    outBands[b] = outBands[b] / (float)frames / (float)bins;
  }

  free(vReal);
  free(vImag);
  free(i2sBuf);
  i2s_teardown();
  return true;
}

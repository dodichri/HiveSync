#include "sensors.h"

#include <OneWire.h>
#include <DallasTemperature.h>
#include <HX711.h>
#include <Preferences.h>

#include "buttons.h"
#include "display.h"
#include "theme.h"

static OneWire oneWire(DS18B20_PIN);
static DallasTemperature ds18b20(&oneWire);
static HX711 hx711;
static Preferences prefs;

struct HX711Cal {
  bool loaded;
  long offset;
  float scale;
};
static HX711Cal g_hxCal = {false, 0, 0.0f};

// Optional compile-time calibration
#ifndef HX711_CAL_WEIGHT
#define HX711_CAL_WEIGHT 0.0f
#endif

static void loadHXCal() {
  prefs.begin("hivesync", true);
  bool hasOff = prefs.isKey("hx_off");
  bool hasScl = prefs.isKey("hx_scl");
  if (hasOff && hasScl) {
    long long off = prefs.getLong("hx_off", 0);
    float scl = prefs.getFloat("hx_scl", 0.0f);
    if (scl != 0.0f) {
      g_hxCal.loaded = true;
      g_hxCal.offset = (long)off;
      g_hxCal.scale = scl;
    }
  }
  prefs.end();
}

static void saveHXCal(long offset, float scale) {
  prefs.begin("hivesync", false);
  prefs.putLong("hx_off", (long long)offset);
  prefs.putFloat("hx_scl", scale);
  prefs.end();
  g_hxCal.loaded = true;
  g_hxCal.offset = offset;
  g_hxCal.scale = scale;
}

void sensors_init() {
  // Init HX711 so readiness checks work
  hx711.begin(HX711_DOUT_PIN, HX711_SCK_PIN);
  loadHXCal();
  if (g_hxCal.loaded) {
    hx711.set_offset(g_hxCal.offset);
    hx711.set_scale(g_hxCal.scale);
  }
}

bool sensors_readDS18B20C(float &outC) {
  ds18b20.begin();
  int count = ds18b20.getDeviceCount();
  if (count <= 0) {
    return false;
  }
  ds18b20.requestTemperatures();
  float t = ds18b20.getTempCByIndex(0);
  if (t == DEVICE_DISCONNECTED_C) {
    return false;
  }
  outC = t;
  return true;
}

bool sensors_readHX711(long &outRaw, bool &hasUnits, float &outUnits, int samples) {
  if (!hx711.is_ready()) {
    // try to initialize and wait briefly
    hx711.begin(HX711_DOUT_PIN, HX711_SCK_PIN);
  }
  if (!hx711.wait_ready_timeout(1000)) {
    return false;
  }
  outRaw = hx711.read_average(samples);
  if (g_hxCal.loaded) {
    hx711.set_scale(g_hxCal.scale);
    hx711.set_offset(g_hxCal.offset);
    outUnits = hx711.get_units(samples);
    hasUnits = true;
  } else {
#if defined(HX711_SCALE) && defined(HX711_OFFSET)
    hx711.set_scale((float)HX711_SCALE);
    hx711.set_offset((long)HX711_OFFSET);
    outUnits = hx711.get_units(samples);
    hasUnits = true;
#else
    hasUnits = false;
#endif
  }
  return true;
}

bool sensors_runHX711Calibration() {
  display_fillScreen(ST77XX_BLACK);
  display_printAt("HiveSync", TFT_LINE_1, THEME_TEXT_BRAND);
  display_printAt("Calibrate HX711", TFT_LINE_2, THEME_TEXT_PRIMARY);
  display_printAt("Release button...", TFT_LINE_3, THEME_TEXT_ACCENT);
  display_drawBatteryTopRight();

  pinMode(CAL_BTN_PIN, CAL_BTN_INPUT_MODE);
  buttons_waitRelease(CAL_BTN_PIN, CAL_BTN_ACTIVE_LEVEL);
  delay(150);

  // Step 1: Tare (offset)
  display_fillScreen(ST77XX_BLACK);
  display_printAt("Cal: Step 1/2", TFT_LINE_1, THEME_TEXT_BRAND);
  display_printAt("Remove all weight", TFT_LINE_2, THEME_TEXT_PRIMARY);
  display_printAt("Press to zero", TFT_LINE_3, THEME_TEXT_ACCENT);
  display_drawBatteryTopRight();
  buttons_waitPress(CAL_BTN_PIN, CAL_BTN_ACTIVE_LEVEL);
  buttons_waitRelease(CAL_BTN_PIN, CAL_BTN_ACTIVE_LEVEL);
  if (!hx711.wait_ready_timeout(2000)) {
    display_fillScreen(ST77XX_BLACK);
    display_printAt("HX711 not ready", TFT_LINE_2, THEME_TEXT_ERROR);
    display_drawBatteryTopRight();
    delay(1200);
    return false;
  }
  hx711.tare(15);
  long offset = hx711.get_offset();

  // Step 2: Known weight (user selects with D2)
  pinMode(SEL_BTN_PIN, SEL_BTN_INPUT_MODE);
  float selWeight = HX711_CAL_WEIGHT;
  display_fillScreen(ST77XX_BLACK);
  display_printAt("Cal: Step 2/2", TFT_LINE_1, THEME_TEXT_BRAND);
  char wline[40];
  snprintf(wline, sizeof(wline), "Weight: %.0f %s", selWeight, HX711_UNITS_LABEL);
  display_printAt(String(wline), TFT_LINE_2, THEME_TEXT_PRIMARY);
  display_printAt("D2:+1  D1:OK", TFT_LINE_4, THEME_TEXT_ACCENT);
  display_drawBatteryTopRight();
  for (;;) {
    if (buttons_pressed(CAL_BTN_PIN, CAL_BTN_ACTIVE_LEVEL)) {
      buttons_waitRelease(CAL_BTN_PIN, CAL_BTN_ACTIVE_LEVEL);
      break;
    }
    if (buttons_pressed(SEL_BTN_PIN, SEL_BTN_ACTIVE_LEVEL)) {
      selWeight += 1.0f;
      if (selWeight < 1.0f) selWeight = 1.0f;
      display_fillRect(0, TFT_LINE_2 - 20, 240, 32, ST77XX_BLACK);
      snprintf(wline, sizeof(wline), "Weight: %.0f %s", selWeight, HX711_UNITS_LABEL);
      display_printAt(String(wline), TFT_LINE_2, THEME_TEXT_PRIMARY);
      Serial.printf("Calibration weight set: %.0f %s\n", selWeight, HX711_UNITS_LABEL);
      buttons_waitRelease(SEL_BTN_PIN, SEL_BTN_ACTIVE_LEVEL);
      delay(50);
    }
    delay(15);
  }
  if (!hx711.wait_ready_timeout(3000)) {
    display_fillScreen(ST77XX_BLACK);
    display_printAt("HX711 not ready", TFT_LINE_2, THEME_TEXT_ERROR);
    display_drawBatteryTopRight();
    delay(1200);
    return false;
  }
  long raw = hx711.read_average(15);
  if (selWeight <= 0.0f) selWeight = 1.0f;
  float scale = (raw - (float)offset) / selWeight;
  if (scale == 0.0f) scale = 1.0f;
  hx711.set_offset(offset);
  hx711.set_scale(scale);
  float check = hx711.get_units(10);

  // Save to NVS
  saveHXCal(offset, scale);

  // Show result
  char res1[40];
  snprintf(res1, sizeof(res1), "Zero: %ld", offset);
  char res2[40];
  snprintf(res2, sizeof(res2), "Scale: %.3f cnt/%s", scale, HX711_UNITS_LABEL);
  char res3[40];
  snprintf(res3, sizeof(res3), "Reads: %.1f %s", check, HX711_UNITS_LABEL);
  display_fillScreen(ST77XX_BLACK);
  display_printAt("Saved calibration", TFT_LINE_1, THEME_TEXT_BRAND);
  display_printAt(String(res1), TFT_LINE_2, THEME_TEXT_PRIMARY);
  display_printAt(String(res2), TFT_LINE_3, THEME_TEXT_PRIMARY);
  display_printAt(String(res3), TFT_LINE_4, THEME_TEXT_ACCENT);
  display_drawBatteryTopRight();
  delay(1500);
  return true;
}

void sensors_powerDown() {
  hx711.power_down();
}

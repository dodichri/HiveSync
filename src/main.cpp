// HiveSync: BLE Wi-Fi provisioning with QR on TFT
#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiProv.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <qrcode_st7789.h>
// Use Adafruit GFX FreeFont: FreeSans12pt7b
#include <Fonts/FreeSans12pt7b.h>

// DS18B20 temp sensor
#include <OneWire.h>
#include <DallasTemperature.h>
#include "esp_sleep.h"
// HX711 weight sensor
#include <HX711.h>
// NVS key/value storage
#include <Preferences.h>

// Define y-coordinates for TFT text lines
// Keep exact values used throughout to preserve layout
#define TFT_LINE_1  18
#define TFT_LINE_2  42
#define TFT_LINE_3  66
#define TFT_LINE_4  90
#define TFT_LINE_5  114

// Pins come from the board variant for Adafruit ESP32-S3 Reverse TFT Feather
// TFT_CS, TFT_DC, TFT_RST, TFT_BACKLITE, SCK, MISO, MOSI are defined by the variant
#ifndef TFT_CS
#error "This sketch requires a board variant defining TFT pins (TFT_CS/DC/RST/BACKLITE)."
#endif

Adafruit_ST7789 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
QRcode_ST7789 qrcode(&tft);

// Globals for device identity
String g_deviceName;  // HiveSync-<last4>
String g_pop;         // Hive-<last6>

// Run-once flags
static bool g_pendingSampleAfterIP = false;
static bool g_sampleDone = false;

// DS18B20 configuration
#ifndef DS18B20_PIN
// Default to Feather ESP32-S3 user pin D9. Change as needed.
#define DS18B20_PIN 9
#endif

OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);

// HX711 configuration (override via build flags or here)
#ifndef HX711_DOUT_PIN
#define HX711_DOUT_PIN 10
#endif
#ifndef HX711_SCK_PIN
#define HX711_SCK_PIN 11
#endif
// Optional calibration: define HX711_SCALE (units per ADC count) and HX711_OFFSET (raw zero)
// Example: -DHX711_SCALE=2280.0 -DHX711_OFFSET=8390000 -DHX711_UNITS_LABEL=\"g\"
#ifndef HX711_UNITS_LABEL
#define HX711_UNITS_LABEL "lbs"
#endif

static HX711 hx711;
static Preferences prefs;

// Calibration storage
struct HX711Cal {
  bool loaded;
  long offset;
  float scale;
};
static HX711Cal g_hxCal = {false, 0, 0.0f};

// Calibration constants
#ifndef HX711_CAL_WEIGHT
#define HX711_CAL_WEIGHT 0.0f  // known weight value in HX711_UNITS_LABEL units
#endif

// Boot button hold thresholds (ms)
#define CLEAR_PROV_HOLD_MS 2500
#define CALIBRATE_HOLD_MS  6000

// Left-align text at the display's left edge (no centering)
static void tftPrint(const String &text, int16_t y, uint16_t color = ST77XX_WHITE) {
  tft.setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  // For GFX fonts, glyphs can extend left of the cursor. Shift so the
  // leftmost pixel of the text sits at x=0 without centering.
  tft.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  int16_t x = -x1;  // align left edge at x=0
  if (x < 0) x = 0;
  tft.setCursor(x, y);
  tft.print(text);
}

static void displayQRPayload(const String &payload) {
  // Initialize QR rendering and draw
  qrcode.init();
  qrcode.create(payload.c_str());
}

static void displayIP(const IPAddress &ip) {
  tft.fillScreen(ST77XX_BLACK);
  tftPrint("HiveSync", TFT_LINE_1, ST77XX_YELLOW);
  tftPrint(ip.toString(), TFT_LINE_2, ST77XX_CYAN);
}

static void displaySensorsAndSleep(float tempC, const char* weightLine) {
  tft.fillScreen(ST77XX_BLACK);
  tftPrint("HiveSync", TFT_LINE_1, ST77XX_YELLOW);
  char buf[32];
  snprintf(buf, sizeof(buf), "Temp: %.2f C", tempC);
  tftPrint(String(buf), TFT_LINE_2, ST77XX_WHITE);
  tftPrint(String(weightLine), TFT_LINE_3, ST77XX_WHITE);
  tftPrint("Sleeping 15 min...", TFT_LINE_4, ST77XX_CYAN);
}

static String buildQRPayload(const String &name, const String &pop, const char *transport) {
  // Mirrors WiFiProv.printQR() payload
  String payload = "{";
  payload += "\"ver\":\"v1\",";
  payload += "\"name\":\"" + name + "\",";
  if (pop.length()) {
    payload += "\"pop\":\"" + pop + "\",";
  }
  payload += "\"transport\":\"";
  payload += transport;
  payload += "\"}";
  return payload;
}

static String cleanMacLastN(uint8_t n) {
  String mac = WiFi.macAddress(); // e.g. AA:BB:CC:DD:EE:FF
  mac.replace(":", "");
  mac.toUpperCase();
  if (mac.length() < n) return mac;
  return mac.substring(mac.length() - n);
}

static void powerOnDisplay() {
#ifdef TFT_I2C_POWER
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
#endif
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);
}

// Button pins
#ifndef BOOT_BTN_PIN
#define BOOT_BTN_PIN 0  // D0: boot/clear provisioning
#endif
#ifndef CAL_BTN_PIN
#ifdef D1
#define CAL_BTN_PIN D1  // Use D1 for calibration
#else
#define CAL_BTN_PIN 1   // fallback to GPIO1 if D1 alias not present
#endif
#endif

// Increment/select button (D2) for calibration weight
#ifndef SEL_BTN_PIN
#ifdef D2
#define SEL_BTN_PIN D2
#else
#define SEL_BTN_PIN 2   // fallback to GPIO2 if D2 alias not present
#endif
#endif

// Button electrical configuration
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

// Generic button helpers
static inline bool btnPressed(uint8_t pin, int activeLevel) { return digitalRead(pin) == activeLevel; }
static void waitBtnRelease(uint8_t pin, int activeLevel) { while (btnPressed(pin, activeLevel)) delay(10); }
static void waitBtnPress(uint8_t pin, int activeLevel) { while (!btnPressed(pin, activeLevel)) delay(10); }

// Measure how long a button is held at startup (up to max_ms)
static uint32_t measureHoldMs(uint8_t pin, uint32_t max_ms, uint8_t inputMode, int activeLevel) {
  pinMode(pin, inputMode);
  if (!btnPressed(pin, activeLevel)) return 0;
  uint32_t start = millis();
  while (btnPressed(pin, activeLevel) && (millis() - start) < max_ms) {
    delay(10);
  }
  return millis() - start;
}

// Provisioning/WiFi event handler
void SysProvEvent(arduino_event_t *sys_event) {
  switch (sys_event->event_id) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
      IPAddress ip(sys_event->event_info.got_ip.ip_info.ip.addr);
      Serial.print("Connected IP address: ");
      Serial.println(ip);
      displayIP(ip);
      // Mark ready to read sensors now that WiFi is connected
      g_pendingSampleAfterIP = true;
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("WiFi disconnected. Reconnecting...");
      break;
    case ARDUINO_EVENT_PROV_START: {
      Serial.println("Provisioning started. Use the app to provision.");
      // Show QR code on TFT
      String payload = buildQRPayload(g_deviceName, g_pop, "ble");
      displayQRPayload(payload);
      // Also log the QR in serial (for convenience)
      WiFiProv.printQR(g_deviceName.c_str(), g_pop.c_str(), "ble");
      break;
    }
    case ARDUINO_EVENT_PROV_CRED_RECV:
      Serial.printf("Received Wi-Fi credentials\n\tSSID: %s\n\tPassword: %s\n",
                    (const char *)sys_event->event_info.prov_cred_recv.ssid,
                    (const char *)sys_event->event_info.prov_cred_recv.password);
      break;
    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
      Serial.println("Provisioning successful");
      break;
    case ARDUINO_EVENT_PROV_CRED_FAIL:
      Serial.println("Provisioning failed. Reset to factory and retry.");
      if (sys_event->event_info.prov_fail_reason == WIFI_PROV_STA_AUTH_ERROR)
        Serial.println("Reason: Wi-Fi AP password incorrect");
      else
        Serial.println("Reason: AP not found or other error");
      break;
    case ARDUINO_EVENT_PROV_END:
      Serial.println("Provisioning ended");
      break;
    default:
      break;
  }
}

// Return true if D0 is held LOW for hold_ms at boot
static bool bootLongPressToClear(uint32_t hold_ms = CLEAR_PROV_HOLD_MS) {
  return measureHoldMs(BOOT_BTN_PIN, hold_ms + 100, BOOT_BTN_INPUT_MODE, BOOT_BTN_ACTIVE_LEVEL) >= hold_ms;
}

// Read first DS18B20 temperature in Celsius, return true if success
static bool readDS18B20C(float &outC) {
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

// Read HX711. Returns true if sensor responded; outputs raw and, when calibrated, units.
static bool readHX711(long &outRaw, bool &hasUnits, float &outUnits, int samples = 10) {
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

// Load/store calibration from NVS
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

// Simple two-step calibration UI using the boot button
static bool runHX711Calibration() {
  tft.fillScreen(ST77XX_BLACK);
  tftPrint("HiveSync", TFT_LINE_1, ST77XX_YELLOW);
  tftPrint("Calibrate HX711", TFT_LINE_2, ST77XX_WHITE);
  tftPrint("Release button...", TFT_LINE_3, ST77XX_CYAN);
  // Ensure correct input mode for calibration button
  pinMode(CAL_BTN_PIN, CAL_BTN_INPUT_MODE);
  waitBtnRelease(CAL_BTN_PIN, CAL_BTN_ACTIVE_LEVEL);
  delay(150);

  // Step 1: Tare (offset)
  tft.fillScreen(ST77XX_BLACK);
  tftPrint("Cal: Step 1/2", TFT_LINE_1, ST77XX_YELLOW);
  tftPrint("Remove all weight", TFT_LINE_2, ST77XX_WHITE);
  tftPrint("Press to zero", TFT_LINE_3, ST77XX_CYAN);
  waitBtnPress(CAL_BTN_PIN, CAL_BTN_ACTIVE_LEVEL);
  waitBtnRelease(CAL_BTN_PIN, CAL_BTN_ACTIVE_LEVEL);
  if (!hx711.wait_ready_timeout(2000)) {
    tft.fillScreen(ST77XX_BLACK);
    tftPrint("HX711 not ready", TFT_LINE_2, ST77XX_RED);
    delay(1200);
    return false;
  }
  hx711.tare(15);
  long offset = hx711.get_offset();

  // Step 2: Known weight (user selects with D2)
  pinMode(SEL_BTN_PIN, SEL_BTN_INPUT_MODE);
  float selWeight = HX711_CAL_WEIGHT;
  tft.fillScreen(ST77XX_BLACK);
  tftPrint("Cal: Step 2/2", TFT_LINE_1, ST77XX_YELLOW);
  char wline[40];
  snprintf(wline, sizeof(wline), "Weight: %.0f %s", selWeight, HX711_UNITS_LABEL);
  tftPrint(String(wline), TFT_LINE_2, ST77XX_WHITE);
  tftPrint("D2:+1  D1:OK", TFT_LINE_4, ST77XX_CYAN);
  for (;;) {
    if (btnPressed(CAL_BTN_PIN, CAL_BTN_ACTIVE_LEVEL)) {
      waitBtnRelease(CAL_BTN_PIN, CAL_BTN_ACTIVE_LEVEL);
      break;
    }
    if (btnPressed(SEL_BTN_PIN, SEL_BTN_ACTIVE_LEVEL)) {
      selWeight += 1.0f;
      if (selWeight < 1.0f) selWeight = 1.0f;
      tft.fillRect(0, TFT_LINE_2 - 20, 240, 32, ST77XX_BLACK);
      snprintf(wline, sizeof(wline), "Weight: %.0f %s", selWeight, HX711_UNITS_LABEL);
      tftPrint(String(wline), TFT_LINE_2, ST77XX_WHITE);
      Serial.printf("Calibration weight set: %.0f %s\n", selWeight, HX711_UNITS_LABEL);
      waitBtnRelease(SEL_BTN_PIN, SEL_BTN_ACTIVE_LEVEL);
      delay(50);
    }
    delay(15);
  }
  if (!hx711.wait_ready_timeout(3000)) {
    tft.fillScreen(ST77XX_BLACK);
    tftPrint("HX711 not ready", TFT_LINE_2, ST77XX_RED);
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
  tft.fillScreen(ST77XX_BLACK);
  tftPrint("Saved calibration", TFT_LINE_1, ST77XX_YELLOW);
  tftPrint(String(res1), TFT_LINE_2, ST77XX_WHITE);
  tftPrint(String(res2), TFT_LINE_3, ST77XX_WHITE);
  tftPrint(String(res3), TFT_LINE_4, ST77XX_CYAN);
  delay(1500);
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("HiveSync starting...");

  // Compute identity strings from MAC
  String mac4 = cleanMacLastN(4);
  String mac6 = cleanMacLastN(6);
  g_deviceName = String("HiveSync-") + mac4; // Device service name
  g_pop = String("Hive-") + mac6;           // Proof-of-possession

  // Bring up TFT early with backlight
  powerOnDisplay();
  // Initialize SPI with variant pins
  SPI.begin(SCK, MISO, MOSI, SS);
  // Initialize ST7789. For this board it's 240x135; rotate for landscape
  tft.init(135, 240);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);
  // Select FreeSans 12pt GFX font for all subsequent text
  tft.setFont(&FreeSans12pt7b);
  tftPrint("HiveSync", TFT_LINE_1, ST77XX_YELLOW);
  tftPrint("Waiting...", TFT_LINE_2, ST77XX_WHITE);

  // Init HX711 pins early so readiness checks work
  hx711.begin(HX711_DOUT_PIN, HX711_SCK_PIN);
  // Load saved calibration if present
  loadHXCal();
  if (g_hxCal.loaded) {
    hx711.set_offset(g_hxCal.offset);
    hx711.set_scale(g_hxCal.scale);
  }

  // Configure button pull modes up-front
  pinMode(BOOT_BTN_PIN, BOOT_BTN_INPUT_MODE);
  pinMode(CAL_BTN_PIN, CAL_BTN_INPUT_MODE);
  pinMode(SEL_BTN_PIN, SEL_BTN_INPUT_MODE);

  // Boot button actions: hold for clear or calibrate
  bool resetProv = false;
  uint32_t heldCal = measureHoldMs(CAL_BTN_PIN, 9000, CAL_BTN_INPUT_MODE, CAL_BTN_ACTIVE_LEVEL);
  if (heldCal >= CALIBRATE_HOLD_MS) {
    Serial.println("Entering HX711 calibration mode (long hold)");
    runHX711Calibration();
  } else if (bootLongPressToClear(CLEAR_PROV_HOLD_MS)) {
    resetProv = true;
    tft.fillScreen(ST77XX_BLACK);
    tftPrint("HiveSync", TFT_LINE_1, ST77XX_YELLOW);
    tftPrint("Clearing provisioning...", TFT_LINE_2, ST77XX_RED);
    Serial.println("Long press detected on D0: clearing provisioning");
    delay(300);
  }

  // Register provisioning/WiFi events
  WiFi.onEvent(SysProvEvent);

  // Start BLE provisioning. Credentials persist via NVS by default.
  // Using BLE scheme with security 1 (PoP) and our custom service name / PoP.
  uint8_t uuid[16] = {0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
                      0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02};
  WiFiProv.beginProvision(
      WIFI_PROV_SCHEME_BLE,
      WIFI_PROV_SCHEME_HANDLER_FREE_BTDM,
      WIFI_PROV_SECURITY_1,
      g_pop.c_str(),
      g_deviceName.c_str(),
      nullptr,
      uuid,
      resetProv  // clear provisioning when D0 held during boot
  );
}

void loop() {
  // After WiFi got IP, perform one sensor read then deep sleep
  if (g_pendingSampleAfterIP && !g_sampleDone) {
    g_sampleDone = true;
    float tempC = NAN;
    bool ok = readDS18B20C(tempC);
    if (ok) {
      Serial.printf("DS18B20 temperature: %.2f C\n", tempC);
    } else {
      Serial.println("No DS18B20 detected or read failed.");
    }

    long hxRaw = 0;
    bool hxHasUnits = false;
    float hxUnits = 0.0f;
    bool hxOK = readHX711(hxRaw, hxHasUnits, hxUnits, 10);
    char weightLine[40];
    if (hxOK) {
      if (hxHasUnits) {
        snprintf(weightLine, sizeof(weightLine), "Wt: %.2f %s", hxUnits, HX711_UNITS_LABEL);
        Serial.printf("HX711: %.2f %s (raw %ld)\n", hxUnits, HX711_UNITS_LABEL, hxRaw);
      } else {
        snprintf(weightLine, sizeof(weightLine), "Wt raw: %ld", hxRaw);
        Serial.printf("HX711 raw: %ld (calibrate to get units)\n", hxRaw);
      }
    } else {
      snprintf(weightLine, sizeof(weightLine), "HX711 not ready");
      Serial.println("HX711 not ready or not connected.");
    }

    // Show readings and sleep
    if (ok) {
      displaySensorsAndSleep(tempC, weightLine);
    } else {
      tft.fillScreen(ST77XX_BLACK);
      tftPrint("HiveSync", TFT_LINE_1, ST77XX_YELLOW);
      tftPrint("Temp sensor missing", TFT_LINE_2, ST77XX_RED);
      tftPrint(String(weightLine), TFT_LINE_3, ST77XX_WHITE);
      tftPrint("Sleeping 15 min...", TFT_LINE_4, ST77XX_CYAN);
    }
    const uint64_t sleep_us = 15ULL * 60ULL * 1000000ULL;
    esp_sleep_enable_timer_wakeup(sleep_us);
    // Power down peripherals where possible
    hx711.power_down();
    digitalWrite(TFT_BACKLITE, LOW);
    Serial.println("Entering deep sleep for 15 minutes...");
    Serial.flush();
    delay(250);
    esp_deep_sleep_start();
  }
  delay(50);
}

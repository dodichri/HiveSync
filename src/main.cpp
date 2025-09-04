// HiveSync: BLE Wi-Fi provisioning with QR on TFT
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiProv.h>
#include "esp_sleep.h"

#include "display.h"
#include "buttons.h"
#include "sensors.h"
// INMP441 I2S microphone + FFT
#include "audio_inmp441.h"
#include "beep_client.h"

// Globals for device identity
String g_deviceName;  // HiveSync-<last4>
String g_pop;         // Hive-<last6>

// Run-once flags
static bool g_pendingSampleAfterIP = false;
static bool g_sampleDone = false;

// Boot button hold thresholds (ms)
#define CLEAR_PROV_HOLD_MS 2500
#define CALIBRATE_HOLD_MS  6000

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

// Provisioning/WiFi event handler
void SysProvEvent(arduino_event_t *sys_event) {
  switch (sys_event->event_id) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
      IPAddress ip(sys_event->event_info.got_ip.ip_info.ip.addr);
      Serial.print("Connected IP address: ");
      Serial.println(ip);
      display_showIP(ip);
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
      display_showQR(payload);
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

// Return true if D0 is held for hold_ms at boot
static bool bootLongPressToClear(uint32_t hold_ms = CLEAR_PROV_HOLD_MS) {
  return buttons_measureHoldMs(BOOT_BTN_PIN, hold_ms + 100, BOOT_BTN_INPUT_MODE, BOOT_BTN_ACTIVE_LEVEL) >= hold_ms;
}

// Removed sensor helpers and calibration UI (moved to sensors module)

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

  // Bring up display
  display_init();
  display_printAt("HiveSync", TFT_LINE_1, COLOR_HIVE_YELLOW);
  display_printAt("Waiting...", TFT_LINE_2, COLOR_WHITE_SMOKE);

  // Init sensors (HX711 + calibration)
  sensors_init();

  // Configure button pull modes up-front
  buttons_setupPins();

  // Boot button actions: hold for clear or calibrate
  bool resetProv = false;
  uint32_t heldCal = buttons_measureHoldMs(CAL_BTN_PIN, 9000, CAL_BTN_INPUT_MODE, CAL_BTN_ACTIVE_LEVEL);
  if (heldCal >= CALIBRATE_HOLD_MS) {
    Serial.println("Entering HX711 calibration mode (long hold)");
    sensors_runHX711Calibration();
  } else if (bootLongPressToClear(CLEAR_PROV_HOLD_MS)) {
    resetProv = true;
    display_fillScreen(ST77XX_BLACK);
    display_printAt("HiveSync", TFT_LINE_1, COLOR_HIVE_YELLOW);
    display_printAt("Clearing provisioning...", TFT_LINE_2, ST77XX_RED);
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
    bool ok = sensors_readDS18B20C(tempC);
    if (ok) {
      Serial.printf("DS18B20 temperature: %.2f C\n", tempC);
    } else {
      Serial.println("No DS18B20 detected or read failed.");
    }

    long hxRaw = 0;
    bool hxHasUnits = false;
    float hxUnits = 0.0f;
    bool hxOK = sensors_readHX711(hxRaw, hxHasUnits, hxUnits, 10);
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

    // Optionally record/analyze 60s of audio into defined FFT bands
    float bands[AUDIO_BANDS] = {0};
    display_printAt("Audio: 60s capture...", TFT_LINE_5, COLOR_WHITE_SMOKE);
    bool audioOK = analyzeINMP441Bins60s(bands);
    if (audioOK) {
      // Print named bins in requested ranges
      Serial.printf("s_bin098_146Hz: %.2f\n", bands[0]);
      Serial.printf("s_bin146_195Hz: %.2f\n", bands[1]);
      Serial.printf("s_bin195_244Hz: %.2f\n", bands[2]);
      Serial.printf("s_bin244_293Hz: %.2f\n", bands[3]);
      Serial.printf("s_bin293_342Hz: %.2f\n", bands[4]);
      Serial.printf("s_bin342_391Hz: %.2f\n", bands[5]);
      Serial.printf("s_bin391_439Hz: %.2f\n", bands[6]);
      Serial.printf("s_bin439_488Hz: %.2f\n", bands[7]);
      Serial.printf("s_bin488_537Hz: %.2f\n", bands[8]);
      Serial.printf("s_bin537_586Hz: %.2f\n", bands[9]);
    } else {
      Serial.println("I2S microphone not initialized (check pins/wiring). Skipping audio.");
    }

    // Send to BEEP.nl API
    {
      bool sent = beep_sendReadings(
          tempC,
          ok,
          hxOK,
          hxHasUnits,
          hxRaw,
          hxUnits,
          audioOK,
          bands,
          (size_t)AUDIO_BANDS);
      if (!sent) {
        Serial.println("Failed to send measurements to BEEP");
      }
    }

    // Show readings and sleep
    if (ok) {
      display_showSensorsAndSleep(tempC, weightLine);
    } else {
      display_fillScreen(ST77XX_BLACK);
      display_printAt("HiveSync", TFT_LINE_1, COLOR_HIVE_YELLOW);
      display_printAt("Temp sensor missing", TFT_LINE_2, ST77XX_RED);
      display_printAt(String(weightLine), TFT_LINE_3, COLOR_WHITE_SMOKE);
      display_printAt("Sleeping 15 min...", TFT_LINE_4, COLOR_SIGNAL_BLUE);
    }
    const uint64_t sleep_us = 15ULL * 60ULL * 1000000ULL;
    esp_sleep_enable_timer_wakeup(sleep_us);
    // Power down peripherals where possible
    sensors_powerDown();
    display_backlight(false);
    Serial.println("Entering deep sleep for 15 minutes...");
    Serial.flush();
    delay(250);
    esp_deep_sleep_start();
  }
  delay(50);
}

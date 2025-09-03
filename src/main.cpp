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

// Provisioning/WiFi event handler
void SysProvEvent(arduino_event_t *sys_event) {
  switch (sys_event->event_id) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
      IPAddress ip(sys_event->event_info.got_ip.ip_info.ip.addr);
      Serial.print("Connected IP address: ");
      Serial.println(ip);
      displayIP(ip);
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
static bool bootLongPressToClear(uint32_t hold_ms = 2000) {
  pinMode(0, INPUT_PULLUP);
  // Require button to be already pressed (LOW) at boot
  if (digitalRead(0) != LOW) return false;
  uint32_t start = millis();
  while (millis() - start < hold_ms) {
    if (digitalRead(0) != LOW) {
      return false;  // released before hold time
    }
    delay(10);
  }
  return true;  // held long enough
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

  // Option on boot: long-press D0 to clear provisioning
  bool resetProv = false;
  if (bootLongPressToClear(2500)) {
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
  delay(100);
}

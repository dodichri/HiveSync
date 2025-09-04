#include "beep_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

#include "sensors.h" // for HX711_UNITS_LABEL if available

#include "secrets.h"  // user-provided credentials (create include/secrets.h)

#ifndef BEEP_API_BASE
#define BEEP_API_BASE "https://api.beep.nl/api"
#endif

#ifndef BEEP_MEASUREMENTS_PATH
#define BEEP_MEASUREMENTS_PATH "/measurements"
#endif

#ifndef BEEP_TLS_INSECURE
#define BEEP_TLS_INSECURE 1
#endif

static String buildUrl(const String& base, const String& path) {
  if (base.endsWith("/") && path.startsWith("/")) return base + path.substring(1);
  if (!base.endsWith("/") && !path.startsWith("/")) return base + "/" + path;
  return base + path;
}

static String iso8601utc(time_t t) {
  struct tm tmv;
  gmtime_r(&t, &tmv);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
  return String(buf);
}

static void ensureTimeSynced() {
  // If time not set (year < 2016), try quick sync; non-blocking enough in this flow
  time_t now = time(nullptr);
  if (now > 1451606400UL) return; // 2016-01-01
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");
  // Poll up to ~5s
  for (int i = 0; i < 50; ++i) {
    delay(100);
    now = time(nullptr);
    if (now > 1451606400UL) break;
  }
}

static bool httpPostJson(const String& url, const String* bearerToken, const String& body, int& httpCode, String& resp) {
  WiFiClientSecure client;
#if BEEP_TLS_INSECURE
  client.setInsecure();
#endif
  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("HTTP begin failed: " + url);
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  if (bearerToken && bearerToken->length() > 0) {
    http.addHeader("Authorization", String("Bearer ") + *bearerToken);
  }
  httpCode = http.POST((uint8_t*)body.c_str(), body.length());
  resp = http.getString();
  http.end();
  return httpCode > 0;
}

static bool beep_login(String& apiTokenOut) {
#ifndef BEEP_EMAIL
#error "Please define BEEP_EMAIL in include/secrets.h"
#endif
#ifndef BEEP_PASSWORD
#error "Please define BEEP_PASSWORD in include/secrets.h"
#endif
  if (String(BEEP_EMAIL).length() == 0 || String(BEEP_PASSWORD).length() == 0) {
    Serial.println("BEEP credentials are empty. Fill include/secrets.h");
    return false;
  }

  const String url = buildUrl(BEEP_API_BASE, "/login");
  StaticJsonDocument<256> doc;
  doc["email"] = BEEP_EMAIL;
  doc["password"] = BEEP_PASSWORD;
  String payload;
  serializeJson(doc, payload);

  int code = 0; String resp;
  if (!httpPostJson(url, nullptr, payload, code, resp)) {
    Serial.printf("BEEP login transport error.\n");
    return false;
  }
  if (code < 200 || code >= 300) {
    Serial.printf("BEEP login failed: HTTP %d, body: %s\n", code, resp.c_str());
    return false;
  }

  StaticJsonDocument<512> respDoc;
  DeserializationError err = deserializeJson(respDoc, resp);
  if (err) {
    Serial.printf("BEEP login JSON parse error: %s\n", err.c_str());
    return false;
  }
  const char* tok = respDoc["api_token"] | nullptr;
  if (!tok || !*tok) {
    Serial.println("BEEP login response missing api_token");
    return false;
  }
  apiTokenOut = tok;
  return true;
}

static String buildMeasurementsPayload(
    float tempC, bool tempOK, bool hxOK, bool hxHasUnits, long hxRaw, float hxUnits,
    bool audioOK, const float* bands, size_t bandsCount)
{
  StaticJsonDocument<4096> doc;
  doc["source"] = "HiveSync";
#ifdef BEEP_HIVE_ID
  if (BEEP_HIVE_ID > 0) doc["hive_id"] = BEEP_HIVE_ID;
#endif
  time_t now = time(nullptr);
  if (now > 1451606400UL) doc["measured_at"] = iso8601utc(now);

  JsonObject vals = doc.createNestedObject("values");
  if (tempOK && !isnan(tempC)) vals["temperature_c"] = tempC;
  if (hxOK) {
    vals["hx711_raw"] = hxRaw;
    if (hxHasUnits) {
      vals["weight_value"] = hxUnits;
#ifdef HX711_UNITS_LABEL
      vals["weight_units"] = HX711_UNITS_LABEL;
#else
      vals["weight_units"] = "units";
#endif
    }
  }
  if (audioOK && bands && bandsCount > 0) {
    JsonObject audio = vals.createNestedObject("audio");
    // Expecting AUDIO_BANDS == bandsCount and specific naming used in Serial logs
    const char* names[] = {
      "bin098_146Hz","bin146_195Hz","bin195_244Hz","bin244_293Hz","bin293_342Hz",
      "bin342_391Hz","bin391_439Hz","bin439_488Hz","bin488_537Hz","bin537_586Hz"
    };
    for (size_t i = 0; i < bandsCount && i < (sizeof(names)/sizeof(names[0])); ++i) {
      audio[names[i]] = bands[i];
    }
  }

  String out;
  serializeJson(doc, out);
  return out;
}

static bool beep_postMeasurements(const String& apiToken,
                                  float tempC, bool tempOK, bool hxOK, bool hxHasUnits, long hxRaw, float hxUnits,
                                  bool audioOK, const float* bands, size_t bandsCount) {
  const String url = buildUrl(BEEP_API_BASE, BEEP_MEASUREMENTS_PATH);
  const String payload = buildMeasurementsPayload(tempC, tempOK, hxOK, hxHasUnits, hxRaw, hxUnits, audioOK, bands, bandsCount);
  int code = 0; String resp;
  if (!httpPostJson(url, &apiToken, payload, code, resp)) {
    Serial.println("BEEP post transport error");
    return false;
  }
  if (code < 200 || code >= 300) {
    Serial.printf("BEEP post failed: HTTP %d, body: %s\n", code, resp.c_str());
    return false;
  }
  Serial.println("BEEP post OK");
  return true;
}

bool beep_sendReadings(
    float tempC,
    bool tempOK,
    bool hxOK,
    bool hxHasUnits,
    long hxRaw,
    float hxUnits,
    bool audioOK,
    const float* audioBands,
    size_t bandsCount)
{
  ensureTimeSynced();

  String token;
  if (!beep_login(token)) return false;
  return beep_postMeasurements(token, tempC, tempOK, hxOK, hxHasUnits, hxRaw, hxUnits, audioOK, audioBands, bandsCount);
}


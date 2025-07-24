#include "usermod_v2_bartdepart.h"
#include <cstdint>

const char CFG_NAME[] = "BartDepart";
const char CFG_ENABLED[] = "Enabled";
const char CFG_UPDATE_SECS[] = "UpdateSecs";
const char CFG_API_BASE[] = "ApiBase";
const char CFG_API_KEY[] = "ApiKey";
const char CFG_API_STATION[] = "ApiStation";

const uint16_t SAFETY_DELAY_MSEC = 5000;

static BartDepart bartdepart_usermod;
REGISTER_USERMOD(bartdepart_usermod);

static bool safetyWaitDone = false;
static unsigned long startTs;

BartDepart::~BartDepart() { /* nothing */ }

void BartDepart::setup() {
  // NOTE - if you are using UDP logging the DEBUG_PRINTLN in this
  // routine will likely not show up because this is prior to WiFi
  // being up.

  // NOTE - it's a really bad idea to crash or deadlock in this
  // method; you won't be able to use OTA update and will have to
  // resort to a serial connection to unbrick your controller ...

  DEBUG_PRINTLN(F("BartDepart::setup starting"));

  //Serial.begin(115200);

  // Print version number
  DEBUG_PRINT(F("BartDepart version: "));
  DEBUG_PRINTLN(BARTDEPART_VERSION);

  // Start a nice chase so we know its booting
  DEBUG_PRINTLN(F("BartDepart::showBooting"));
  showBooting();

  client.setInsecure();	// don't validate certs

  startTs = millis();

  DEBUG_PRINTLN(F("BartDepart::setup finished"));
}

void BartDepart::loop() {

  // FIXME: safety delay to allow us to offMode before impending crash
  if (!safetyWaitDone && millis() - startTs >= SAFETY_DELAY_MSEC) {
    safetyWaitDone = true;
  }

  if (safetyWaitDone && enabled && !offMode) {
    if (millis() - lastCheck >= updateSecs * 1000) {
      fetchData();
      lastCheck = millis();
    }
  }
}

// This function is called by WLED when the USERMOD config is read
bool BartDepart::readFromConfig(JsonObject& root) {
  // Attempt to retrieve the nested object for this usermod
  JsonObject top = root[FPSTR(CFG_NAME)];
  bool configComplete = !top.isNull();  // check if the object exists

  // Retrieve the values using the getJsonValue function for better error handling
  configComplete &= getJsonValue(top[FPSTR(CFG_ENABLED)], enabled, enabled);
  configComplete &= getJsonValue(top[FPSTR(CFG_UPDATE_SECS)], updateSecs, updateSecs);
  configComplete &= getJsonValue(top[FPSTR(CFG_API_BASE)], apiBase, apiBase);
  configComplete &= getJsonValue(top[FPSTR(CFG_API_KEY)], apiKey, apiKey);
  configComplete &= getJsonValue(top[FPSTR(CFG_API_STATION)], apiStation, apiStation);

  return configComplete;
}

// This function is called by WLED when the USERMOD config is saved in the frontend
void BartDepart::addToConfig(JsonObject& root) {
  // Create a nested object for this usermod
  JsonObject top = root.createNestedObject(FPSTR(CFG_NAME));

  // Write the configuration parameters to the nested object
  top[FPSTR(CFG_ENABLED)] = enabled;
  top[FPSTR(CFG_UPDATE_SECS)] = updateSecs;
  top[FPSTR(CFG_API_BASE)] = apiBase;
  top[FPSTR(CFG_API_KEY)] = apiKey;
  top[FPSTR(CFG_API_STATION)] = apiStation;

  if (enabled==false)
    // Unfreeze the main segment after disabling the module
    strip.getMainSegment().freeze=false;
}

String BartDepart::composeApiUrl() {
  String url;
  url  = apiBase;
  url += "&key=";
  url += apiKey;
  url += "&orig=";
  url += apiStation;
  return url;
}

void BartDepart::fetchData() {
  unsigned long t0 = millis();
  showLoading();
  String url = composeApiUrl();
  DEBUG_PRINTLN(String(F("BartDepart::fetchData starting ")) + url);

  https.begin(client, url);
  int httpCode = https.GET();
  if (httpCode > 0) {
    // Success—read payload as a String
    String payload = https.getString();
    DEBUG_PRINTLN(String(F("BartDepart::fetchData HTTP: "))
                  + httpCode + F(" ") + payload);
  } else {
    DEBUG_PRINTLN(String(F("BartDepart::fetchData FAILED: "))
                  + https.errorToString(httpCode));
  }
  https.end();
  unsigned long dt = millis() - t0; // elapsed ms
  DEBUG_PRINTLN(String(F("BartDepart::fetchData finished in ")) + dt + F(" ms"));
}

void BartDepart::showBooting() {
  Segment& seg = strip.getMainSegment();
  seg.setMode(28); // Set to chase
  seg.speed = 200;
  seg.intensity = 255;
  seg.setPalette(128);
  seg.setColor(0, 0x404060);
  seg.setColor(1, 0x000000);
  seg.setColor(2, 0x303040);
}

void BartDepart::showLoading() {
  Segment& seg = strip.getMainSegment();
  seg.setMode(28); // Set to chase
  seg.speed = 200;
  seg.intensity = 255;
  seg.setPalette(128);
  seg.setColor(0, 0x604040);
  seg.setColor(1, 0x000000);
  seg.setColor(2, 0x403030);
}

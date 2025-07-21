#include "usermod_v2_bartdepart.h"

// add more strings here to reduce flash memory usage
const char BartDepartUsermod::_name[]    PROGMEM = "BartDepart";
const char BartDepartUsermod::_enabled[] PROGMEM = "Enable";
const char BartDepartUsermod::_api_key[] PROGMEM = "API KEY";
const char BartDepartUsermod::_api_url[] PROGMEM = "API URL";

static BartDepartUsermod bartdepart_usermod;
REGISTER_USERMOD(bartdepart_usermod);

void BartDepartUsermod::setup() {
  DEBUG_PRINTLN(F("BartDepartUsermod::setup starting"));

  //Serial.begin(115200);

  // Print version number
  DEBUG_PRINT(F("BartDepartUsermod version: "));
  DEBUG_PRINTLN(BARTDEPART_VERSION);

  // Start a nice chase so we know its booting and searching for its first http pull.
  DEBUG_PRINTLN(F("Starting a nice chase so we now it is booting."));
  Segment& seg = strip.getMainSegment();
  seg.setMode(28); // Set to chase
  seg.speed = 200;
  seg.intensity = 255;
  seg.setPalette(128);
  seg.setColor(0, 5263440);
  seg.setColor(1, 0);
  seg.setColor(2, 4605510);

  DEBUG_PRINTLN(F("BartDepartUsermod::setup finished"));
}

// This is the main loop function, from here we check the URL and handle the response.
// Effects or individual lights are set as a result from this.
void BartDepartUsermod::loop() {
  if (enabled && !offMode) {
    if (millis() - lastCheck >= checkIntervalSecs * 1000) {
      fetchData();
      lastCheck = millis();
    }
  }
}

// This function is called by WLED when the USERMOD config is read
bool BartDepartUsermod::readFromConfig(JsonObject& root) {
  // Attempt to retrieve the nested object for this usermod
  JsonObject top = root[FPSTR(_name)];
  bool configComplete = !top.isNull();  // check if the object exists

  // Retrieve the values using the getJsonValue function for better error handling
  configComplete &= getJsonValue(top[FPSTR(_enabled)], enabled, enabled);
  configComplete &= getJsonValue(top["checkIntervalSecs"], checkIntervalSecs, checkIntervalSecs);
  configComplete &= getJsonValue(top["url"], url, url);

  return configComplete;
}

// This function is called by WLED when the USERMOD config is saved in the frontend
void BartDepartUsermod::addToConfig(JsonObject& root) {
  // Create a nested object for this usermod
  JsonObject top = root.createNestedObject(FPSTR(_name));

  // Write the configuration parameters to the nested object
  top[FPSTR(_enabled)] = enabled;
  if (enabled==false)
    // Unfreeze the main segment after disabling the module
    strip.getMainSegment().freeze=false;
  top["checkIntervalSecs"] = checkIntervalSecs;
  top["url"] = url;
}

void BartDepartUsermod::fetchData() {
  DEBUG_PRINTLN(F("BartDepartUsermod::fetchData starting"));
  DEBUG_PRINTLN(F("BartDepartUsermod::fetchData finished"));
}

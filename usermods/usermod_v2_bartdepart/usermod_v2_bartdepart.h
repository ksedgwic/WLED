#pragma once

#include "wled.h"
#include "WiFiClientSecure.h"

#define BARTDEPART_VERSION "0.0.1"

class BartDepartUsermod : public Usermod {
private:
  static const char _name[];
  static const char _enabled[];
  static const char _api_key[];
  static const char _api_url[];

  bool enabled = false;

#ifdef BARTDEPART_INTERVAL_SECS
  uint16_t checkIntervalSecs = BARTDEPART_INTERVAL_SECS;
#else
  uint16_t checkIntervalSecs = 60;
#endif

#ifdef BARTDEPART_URL
  String url = BARTDEPART_URL;
#else
  String url = "https://api.bart.gov/api/etd.aspx?cmd=etd&orig=19th&key=MW9S-E7SL-26DU-VV8V&json=y";
#endif

  // Define constants
  static const uint8_t myLockId = USERMOD_ID_BARTDEPART;   // Used for requestJSONBufferLock(id)

  unsigned long lastCheck = 0;        // Timestamp of last check
  String host;                        // Host extracted from the URL
  String path;                        // Path extracted from the URL
  WiFiClientSecure sec_client;

  void fetchData();

public:
  void setup();
  void loop();
  bool readFromConfig(JsonObject &root);
  void addToConfig(JsonObject &root);
  uint16_t getId() { return USERMOD_ID_BARTDEPART; }
  inline void enable(bool enable) { enabled = enable; }
  inline bool isEnabled() { return enabled; }
  virtual ~BartDepartUsermod() { /* nothing */ }
};

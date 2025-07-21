#pragma once

#include "wled.h"
#include "WiFiClientSecure.h"

#define BARTDEPART_VERSION "0.0.1"

class BartDepart : public Usermod {
private:
  bool enabled = false;

#ifdef BARTDEPART_DEFAULT_UPDATE_SECS
  uint16_t updateSecs = BARTDEPART_DEFAULT_UPDATE_SECS;
#else
  uint16_t updateSecs = 60;
#endif

#ifdef BARTDEPART_DEFAULT_API_KEY
  String apiKey = BARTDEPART_DEFAULT_API_KEY;
#else
  String apiKey = "MW9S-E7SL-26DU-VV8V";
#endif

#ifdef BARTDEPART_DEFAULT_API_URL
  String apiUrl = BARTDEPART_DEFAULT_API_URL;
#else
  String apiUrl = "https://api.bart.gov/api/etd.aspx?cmd=etd&orig=19th&key=MW9S-E7SL-26DU-VV8V&json=y";
#endif

  // Define constants
  static const uint8_t myLockId = USERMOD_ID_BARTDEPART;   // Used for requestJSONBufferLock(id)

  unsigned long lastCheck = 0;        // Timestamp of last check
  String host;                        // Host extracted from the URL
  String path;                        // Path extracted from the URL
  WiFiClientSecure client;

  void fetchData();

public:
  void setup();
  void loop();
  bool readFromConfig(JsonObject &root);
  void addToConfig(JsonObject &root);
  uint16_t getId() { return USERMOD_ID_BARTDEPART; }
  inline void enable(bool enable) { enabled = enable; }
  inline bool isEnabled() { return enabled; }
  virtual ~BartDepart();

protected:
  void showBooting();
  void showLoading();
};

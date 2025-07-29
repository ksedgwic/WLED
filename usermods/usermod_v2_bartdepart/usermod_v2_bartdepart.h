#pragma once
#include <memory>

#include "wled.h"
#include "WiFiClientSecure.h"

#if defined(ARDUINO_ARCH_ESP8266)
  #include <ESP8266HTTPClient.h>
#else
  #include <HTTPClient.h>
#endif

#include "train_platform_model.h"

#define BARTDEPART_VERSION "0.0.1"

class BartDepart : public Usermod {
private:
  bool enabled = false;

#ifdef BARTDEPART_DEFAULT_UPDATE_SECS
  uint16_t updateSecs = BARTDEPART_DEFAULT_UPDATE_SECS;
#else
  uint16_t updateSecs = 60;
#endif

#ifdef BARTDEPART_DEFAULT_API_BASE
  String apiBase = BARTDEPART_DEFAULT_API_BASE;
#else
  String apiBase = "https://api.bart.gov/api/etd.aspx?cmd=etd&json=y";
#endif

#ifdef BARTDEPART_DEFAULT_API_KEY
  String apiKey = BARTDEPART_DEFAULT_API_KEY;
#else
  String apiKey = "MW9S-E7SL-26DU-VV8V";
#endif

#ifdef BARTDEPART_DEFAULT_API_STATION
  String apiStation = BARTDEPART_DEFAULT_API_STATION;
#else
  String apiStation = "19th";
#endif

// NOTE - don't use the WLED segment 0, it is "special"

#ifdef BARTDEPART_DEFAULT_SEG1_PLATFORM_ID
  String seg1PlatformId = BARTDEPART_DEFAULT_SEG1_PLATFORM_ID
#else
  String seg1PlatformId = "1";
#endif

#ifdef BARTDEPART_DEFAULT_SEG2_PLATFORM_ID
  String seg2PlatformId = BARTDEPART_DEFAULT_SEG2_PLATFORM_ID
#else
  String seg2PlatformId = "";
#endif

#ifdef BARTDEPART_DEFAULT_SEG3_PLATFORM_ID
  String seg3PlatformId = BARTDEPART_DEFAULT_SEG3_PLATFORM_ID
#else
  String seg3PlatformId = "";
#endif

#ifdef BARTDEPART_DEFAULT_SEG4_PLATFORM_ID
  String seg4PlatformId = BARTDEPART_DEFAULT_SEG4_PLATFORM_ID
#else
  String seg4PlatformId = "";
#endif


  // Define constants
  static const uint8_t myLockId = USERMOD_ID_BARTDEPART;   // Used for requestJSONBufferLock(id)

  unsigned long lastCheck = 0;        // Timestamp of last check
  WiFiClientSecure client;
  HTTPClient https;

  time_t   nextFetchSec    = 0; // when to try the next fetch
  time_t   lastFetchSec    = 0; // time of the most recent attempt
  uint8_t  backoffMult     = 1; // 1,2,4,8,16
  bool     prevOffMode     = false;

  std::vector<TrainPlatformModel> platforms_;

public:
  void setup();
  void loop();
  void handleOverlayDraw() override;
  bool readFromConfig(JsonObject &root);
  void addToConfig(JsonObject &root);
  uint16_t getId() { return USERMOD_ID_BARTDEPART; }
  inline void enable(bool enable) { enabled = enable; }
  inline bool isEnabled() { return enabled; }
  virtual ~BartDepart();

protected:
  std::unique_ptr<DynamicJsonDocument> fetchData();
  String composeApiUrl();
  void showBooting();
  void doneBooting();
};

#pragma once
#include <memory>

#include "wled.h"
#include "WiFiClientSecure.h"
#include <HTTPClient.h>

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

#ifdef BARTDEPART_DEFAULT_PLATFORM_ID
  String platformId = BARTDEPART_DEFAULT_PLATFORM_ID;
#else
  String platformId = "1";
#endif

  // Define constants
  static const uint8_t myLockId = USERMOD_ID_BARTDEPART;   // Used for requestJSONBufferLock(id)

  unsigned long lastCheck = 0;        // Timestamp of last check
  WiFiClientSecure client;
  HTTPClient https;

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

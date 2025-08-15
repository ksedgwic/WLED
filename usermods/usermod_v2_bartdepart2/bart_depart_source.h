#pragma once

#include <memory>
#include <vector>
#include "interfaces.h"
#include "bart_model.h"
#include "wled.h"
#include "WiFiClientSecure.h"
#if defined(ARDUINO_ARCH_ESP8266)
  #include <ESP8266HTTPClient.h>
#else
  #include <HTTPClient.h>
#endif

class BartDepartSource : public IDataSourceT<BartModel> {
private:
  uint16_t updateSecs_ = 60;
  String apiBase_ = "https://api.bart.gov/api/etd.aspx?cmd=etd&json=y";
  String apiKey_ = "MW9S-E7SL-26DU-VV8V";
  String apiStation_ = "19th";
  String seg1PlatformId_ = "1";
  String seg2PlatformId_;
  String seg3PlatformId_;
  String seg4PlatformId_;
  time_t nextFetch_ = 0;
  uint8_t backoffMult_ = 1;
  WiFiClientSecure client_;
  HTTPClient https_;

public:
  BartDepartSource();
  std::unique_ptr<BartModel> fetch(std::time_t now) override;
  std::unique_ptr<BartModel> checkhistory(std::time_t now, std::time_t oldestTstamp) override { return nullptr; }
  void reload(std::time_t now) override;
  std::string name() const override { return "BartDepartSource"; }

  void addToConfig(JsonObject& root) override;
  bool readFromConfig(JsonObject& root, bool startup_complete, bool& invalidate_history) override;
  const char* configKey() const override { return "BartDepartSource"; }

  uint16_t updateSecs() const { return updateSecs_; }
  std::vector<String> platformIds() const {
    return {seg1PlatformId_, seg2PlatformId_, seg3PlatformId_, seg4PlatformId_};
  }
};

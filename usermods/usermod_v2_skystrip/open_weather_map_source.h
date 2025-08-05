#pragma once

#include <ctime>
#include <memory>
#include <string>
#include "interfaces.h"
#include "rest_json_client.h"

class SkyModel;

class OpenWeatherMapSource : public RestJsonClient, public IDataSourceT<SkyModel> {
public:
  OpenWeatherMapSource();

  ~OpenWeatherMapSource() override = default;

  // IDataSourceT<SkyModel>
  std::unique_ptr<SkyModel> fetch(std::time_t now) override;
  std::string name() const override { return "OWM"; }

  // IConfigurable
  void addToConfig(JsonObject& subtree) override;
  bool readFromConfig(JsonObject& subtree) override;
  const char* configKey() const override { return "OpenWeatherMap"; }

  String composeApiUrl();

private:
  std::string apiBase_;
  std::string apiKey_;
  double latitude_;
  double longitude_;
  unsigned int intervalSec_;
  std::time_t lastFetch_;
};

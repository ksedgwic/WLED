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
  void reset(std::time_t now) override;
  std::string name() const override { return "OWM"; }

  // IConfigurable
  void addToConfig(JsonObject& subtree) override;
  bool readFromConfig(JsonObject& subtree, bool startup_complete) override;
  const char* configKey() const override { return "OpenWeatherMap"; }

  String composeApiUrl();
  bool geocodeOWM(std::string const& rawQuery, double& lat, double& lon, int* outMatches = nullptr);

private:
  std::string apiBase_;
  std::string apiKey_;
  std::string location_;
  double latitude_;
  double longitude_;
  unsigned int intervalSec_;
  std::time_t lastFetch_;
  std::string lastLocation_;
};

#pragma once

#include "interfaces.h"
#include "depart_model.h"
#include "util.h"
#include "wled.h"

// Placeholder GTFS-RT source that currently only handles configuration.
class GtfsRtSource : public IDataSourceT<DepartModel> {
private:
  bool     enabled_ = false;
  uint32_t updateSecs_ = 60;
  String   baseUrl_;
  String   apiKey_;
  String   agency_;
  String   stopCode_;
  time_t   nextFetch_ = 0;
  std::string configKey_ = "gtfsrt_source";

public:
  explicit GtfsRtSource(const char* key = "gtfsrt_source");
  std::unique_ptr<DepartModel> fetch(std::time_t now) override;
  void reload(std::time_t now) override;
  std::string name() const override { return configKey_; }
  void appendConfigData(Print& s) override {}
  String sourceKey() const override;
  const char* sourceType() const override { return "gtfsrt"; }

  void addToConfig(JsonObject& root) override;
  bool readFromConfig(JsonObject& root, bool startup_complete, bool& invalidate_history) override;
  const char* configKey() const override { return configKey_.c_str(); }

  const String& agency() const { return agency_; }
};

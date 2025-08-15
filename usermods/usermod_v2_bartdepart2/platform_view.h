#pragma once

#include "interfaces.h"
#include "bart_station_model.h"
#include "util.h"

class PlatformView : public IDataViewT<BartStationModel> {
private:
  uint16_t updateSecs_ = 60;
  String platformId_;
  uint16_t segmentId_;
  std::string configKey_;
public:
  PlatformView(const String& platformId, uint16_t segmentId)
    : platformId_(platformId), segmentId_(segmentId),
      configKey_(std::string("PlatformView") + platformId.c_str()) {}

  void view(std::time_t now, const BartStationModel& model, int16_t dbgPixelIndex) override;
  void addToConfig(JsonObject& root) override { root["SegmentId"] = segmentId_; }
  bool readFromConfig(JsonObject& root, bool startup_complete, bool& invalidate_history) override {
    return getJsonValue(root["SegmentId"], segmentId_, segmentId_);
  }
  const char* configKey() const override { return configKey_.c_str(); }
  std::string name() const override { return configKey_; }
};

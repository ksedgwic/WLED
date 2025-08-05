#include "temperature_view.h"
#include "skymodel.h"

static constexpr int16_t DEFAULT_SEG_ID = -1; // -1 means disabled

// - these are user visible in the webapp settings UI
// - they are scoped to this module, don't need to be globally unique
//
const char CFG_SEG_ID[] = "SegmentId";

TemperatureView::TemperatureView()
  : segId_(DEFAULT_SEG_ID) {
  DEBUG_PRINTLN("SkyStrip: TV::CTOR");
}

void TemperatureView::view(time_t now, SkyModel const & model) {
  // pass
}

void TemperatureView::addToConfig(JsonObject& subtree) {
  subtree[FPSTR(CFG_SEG_ID)] = segId_;
}

bool TemperatureView::readFromConfig(JsonObject& subtree) {
  bool configComplete = !subtree.isNull();
  configComplete &= getJsonValue(subtree[FPSTR(CFG_SEG_ID)], segId_, DEFAULT_SEG_ID);
  return configComplete;
}

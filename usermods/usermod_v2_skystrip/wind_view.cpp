#include "wind_view.h"
#include "skymodel.h"
#include "wled.h"
#include <algorithm>
#include <cmath>
#include "util.h"

static constexpr int16_t DEFAULT_SEG_ID = -1; // -1 means disabled
const char CFG_SEG_ID[] = "SegmentId";

static inline float hueFromDir(float dir) {
  float hue;
  if (dir <= 90.f)
    hue = 240.f + dir * ((30.f + 360.f - 240.f) / 90.f);
  else if (dir <= 180.f)
    hue = 30.f + (dir - 90.f) * ((60.f - 30.f) / 90.f);
  else if (dir <= 270.f)
    hue = 60.f + (dir - 180.f) * ((120.f - 60.f) / 90.f);
  else
    hue = 120.f + (dir - 270.f) * ((240.f - 120.f) / 90.f);
  hue = fmodf(hue, 360.f);
  return hue;
}

static inline float satFromGustDiff(float speed, float gust) {
  float diff = gust - speed; if (diff < 0.f) diff = 0.f;
  constexpr float kMinSat = 0.40f;
  constexpr float kMaxDiff = 20.0f;
  float u = util::clamp01(diff / kMaxDiff);
  float eased = u*u*(3.f - 2.f*u);
  return kMinSat + (1.f - kMinSat) * eased;
}


WindView::WindView()
  : segId_(DEFAULT_SEG_ID) {
  DEBUG_PRINTLN("SkyStrip: WV::CTOR");
}

void WindView::view(time_t now, SkyModel const & model, int16_t dbgPixelIndex) {
  if (segId_ == DEFAULT_SEG_ID) return;
  if (model.wind_speed_forecast.empty()) return;
  if (segId_ < 0 || segId_ >= strip.getMaxSegments()) return;

  Segment &seg = strip.getSegment((uint8_t)segId_);
  seg.freeze = true;
  int start = seg.start;
  int end = seg.stop - 1;
  int len = end - start + 1;
  if (len == 0) return;

  constexpr double kHorizonSec = 48.0 * 3600.0;
  const double step = (len > 1) ? (kHorizonSec / double(len - 1)) : 0.0;

  for (uint16_t i = 0; i < len; ++i) {
    const time_t t = now + time_t(std::llround(step * i));
    double spd, dir, gst;
    if (!util::estimateSpeedAt(model, t, step, spd)) continue;
    if (!util::estimateDirAt(model, t, step, dir)) continue;
    if (!util::estimateGustAt(model, t, step, gst)) gst = spd;
    float hue = hueFromDir((float)dir);
    float sat = satFromGustDiff((float)spd, (float)gst);
    float val = util::clamp01(float(std::max(spd, gst)) / 50.f);
    uint32_t col = util::hsv2rgb(hue, sat, val);
    int idx = seg.reverse ? (end - i) : (start + i);
    strip.setPixelColor(idx, col);
  }
}

void WindView::addToConfig(JsonObject& subtree) {
  subtree[FPSTR(CFG_SEG_ID)] = segId_;
}

void WindView::appendConfigData(Print& s) {
  // Keep the hint INLINE (BEFORE the input = 4th arg):
  s.print(F(
    "addInfo('SkyStrip:WindView:SegmentId',1,'',"
    "'&nbsp;<small style=\\'opacity:.8\\'>(-1 disables)</small>'"
    ");"
  ));
}

bool WindView::readFromConfig(JsonObject& subtree,
                              bool startup_complete,
                              bool& invalidate_history) {
  bool configComplete = !subtree.isNull();
  configComplete &= getJsonValue(subtree[FPSTR(CFG_SEG_ID)], segId_, DEFAULT_SEG_ID);
  return configComplete;
}

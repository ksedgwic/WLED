#include "wind_view.h"
#include "skymodel.h"
#include "wled.h"
#include <algorithm>
#include <cmath>

static constexpr int16_t DEFAULT_SEG_ID = -1; // -1 means disabled
const char CFG_SEG_ID[] = "SegmentId";

static inline double lerp(double a, double b, double t) { return a + (b - a) * t; }
template<typename T> static inline T clamp01(T v) {
  return v < T(0) ? T(0) : (v > T(1) ? T(1) : v);
}

template<class Series>
static bool estimateAt(const Series& v, time_t t, double& out) {
  if (v.empty()) return false;
  if (t <= v.front().tstamp) { out = v.front().value; return true; }
  if (t >= v.back().tstamp)  { out = v.back().value;  return true; }
  for (size_t i = 1; i < v.size(); ++i) {
    if (t <= v[i].tstamp) {
      const auto& a = v[i-1]; const auto& b = v[i];
      const double span = double(b.tstamp - a.tstamp);
      const double u = clamp01(span > 0 ? double(t - a.tstamp) / span : 0.0);
      out = lerp(a.value, b.value, u);
      return true;
    }
  }
  return false;
}

static bool estimateSpeedAt(const SkyModel& m, time_t t, double& out) {
  return estimateAt(m.wind_speed_forecast, t, out);
}
static bool estimateDirAt(const SkyModel& m, time_t t, double& out) {
  return estimateAt(m.wind_dir_forecast, t, out);
}
static bool estimateGustAt(const SkyModel& m, time_t t, double& out) {
  return estimateAt(m.wind_gust_forecast, t, out);
}

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
  float u = clamp01(diff / kMaxDiff);
  float eased = u*u*(3.f - 2.f*u);
  return kMinSat + (1.f - kMinSat) * eased;
}

static uint32_t hsv2rgb(float h, float s, float v) {
  float c = v * s;
  float hh = h / 60.f;
  float x = c * (1.f - fabsf(fmodf(hh, 2.f) - 1.f));
  float r1, g1, b1;
  if (hh < 1.f)      { r1 = c; g1 = x; b1 = 0.f; }
  else if (hh < 2.f) { r1 = x; g1 = c; b1 = 0.f; }
  else if (hh < 3.f) { r1 = 0.f; g1 = c; b1 = x; }
  else if (hh < 4.f) { r1 = 0.f; g1 = x; b1 = c; }
  else if (hh < 5.f) { r1 = x; g1 = 0.f; b1 = c; }
  else               { r1 = c; g1 = 0.f; b1 = x; }
  float m = v - c;
  uint8_t r = uint8_t(lrintf((r1 + m) * 255.f));
  uint8_t g = uint8_t(lrintf((g1 + m) * 255.f));
  uint8_t b = uint8_t(lrintf((b1 + m) * 255.f));
  return RGBW32(r, g, b, 0);
}

WindView::WindView()
  : segId_(DEFAULT_SEG_ID) {
  DEBUG_PRINTLN("SkyStrip: WV::CTOR");
}

void WindView::view(time_t now, SkyModel const & model) {
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
    if (!estimateSpeedAt(model, t, spd)) continue;
    if (!estimateDirAt(model, t, dir)) continue;
    if (!estimateGustAt(model, t, gst)) gst = spd;
    float hue = hueFromDir((float)dir);
    float sat = satFromGustDiff((float)spd, (float)gst);
    float val = clamp01(float(std::max(spd, gst)) / 50.f);
    uint32_t col = hsv2rgb(hue, sat, val);
    strip.setPixelColor(start + i, col);
  }
}

void WindView::addToConfig(JsonObject& subtree) {
  subtree[FPSTR(CFG_SEG_ID)] = segId_;
}

bool WindView::readFromConfig(JsonObject& subtree, bool startup_complete) {
  bool configComplete = !subtree.isNull();
  configComplete &= getJsonValue(subtree[FPSTR(CFG_SEG_ID)], segId_, DEFAULT_SEG_ID);
  return configComplete;
}

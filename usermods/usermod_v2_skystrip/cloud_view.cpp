#include "cloud_view.h"
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

CloudView::CloudView()
  : segId_(DEFAULT_SEG_ID) {
  DEBUG_PRINTLN("SkyStrip: CV::CTOR");
}

void CloudView::view(time_t now, SkyModel const & model) {
  if (segId_ == DEFAULT_SEG_ID) return;
  if (model.cloud_cover_forecast.empty()) return;
  if (segId_ < 0 || segId_ >= strip.getMaxSegments()) return;

  Segment &seg = strip.getSegment((uint8_t)segId_);
  seg.freeze = true;
  int start = seg.start;
  int end = seg.stop - 1;
  int len = end - start + 1;
  if (len == 0) return;

  constexpr double kHorizonSec = 48.0 * 3600.0;
  const double step = (len > 1) ? (kHorizonSec / double(len - 1)) : 0.0;

  for (int i = 0; i < len; ++i) {
    const time_t t = now + time_t(std::llround(step * i));
    double clouds, dayVal, precipTypeVal, precipProb;
    if (!estimateAt(model.cloud_cover_forecast, t, clouds)) continue;
    if (!estimateAt(model.daylight_forecast, t, dayVal)) dayVal = 1.0;
    if (!estimateAt(model.precip_type_forecast, t, precipTypeVal)) precipTypeVal = 0.0;
    if (!estimateAt(model.precip_prob_forecast, t, precipProb)) precipProb = 0.0;

    bool daytime = dayVal >= 0.5;
    float hue = daytime ? 50.f : 265.f;
    float val = daytime ? 0.8f : 0.4f;
    float maxSat = daytime ? 1.f : 0.8f;
    float sat = maxSat * (1.f - clamp01(float(clouds / 100.0)));

    uint32_t col = hsv2rgb(hue, sat, val);

    int p = int(std::round(precipTypeVal));
    if (p != 0 && precipProb > 0.0) {
      float ph, ps, pv;
      if (p == 1)      { ph = 210.f; ps = 1.f;   pv = 1.f; }
      else if (p == 2) { ph = 0.f;   ps = 0.f;   pv = 1.f; }
      else             { ph = 180.f; ps = 0.5f; pv = 1.f; }
      uint32_t pcol = hsv2rgb(ph, ps, pv);
      uint8_t blend = uint8_t(clamp01(float(precipProb)) * 255.f);
      col = color_blend(col, pcol, blend);
    }

    int idx = seg.reverse ? (end - i) : (start + i);
    strip.setPixelColor(idx, col);
  }
}

void CloudView::addToConfig(JsonObject& subtree) {
  subtree[FPSTR(CFG_SEG_ID)] = segId_;
}

bool CloudView::readFromConfig(JsonObject& subtree, bool startup_complete) {
  bool configComplete = !subtree.isNull();
  configComplete &= getJsonValue(subtree[FPSTR(CFG_SEG_ID)], segId_, DEFAULT_SEG_ID);
  return configComplete;
}

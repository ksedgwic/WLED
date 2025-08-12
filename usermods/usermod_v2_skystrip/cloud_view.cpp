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

    // Only light LEDs when there are clouds (or precip present).
    float clouds01 = clamp01(float(clouds / 100.0));
    int p = int(std::round(precipTypeVal));
    if (clouds01 <= 0.f && (p == 0 || precipProb <= 0.0)) {
      int idx = seg.reverse ? (end - i) : (start + i);
      strip.setPixelColor(idx, 0);
      continue;
    }

    uint32_t col = 0;
    // Precipitation has priority: rain=blue, snow=white, mixed=cyan-ish.
    if (p != 0 && precipProb > 0.0) {
      float ph, ps;
      if (p == 1)      { ph = 210.f; ps = 1.0f; }   // rain → blue
      else if (p == 2) { ph = 0.f;   ps = 0.0f; }   // snow → white
      else             { ph = 180.f; ps = 0.5f; }   // mixed
      float pv = clamp01(float(precipProb));        // intensity ~ PoP
      pv = 0.3f + 0.7f * pv;                        // keep visible at low PoP
      col = hsv2rgb(ph, ps, pv);
    } else {
      // No precip: desaturated day/night, intensity tracks cloud cover.
      // Mask very low cloud amounts: below 5% we show nothing.
      constexpr float kCloudMaskThreshold = 0.05f;
      if (clouds01 < kCloudMaskThreshold) {
        int idx = seg.reverse ? (end - i) : (start + i);
        strip.setPixelColor(idx, 0);
        continue;
      }

      const bool daytime = dayVal >= 0.5;
      const float vmax = daytime ? 0.70f : 0.60f;   // cap brightness
      const float vmin = daytime ? 0.18f : 0.00f;   // floor for purity

      float u = clamp01(clouds01);                  // 0..1 cloudiness
      const float shaped = powf(u, 0.8f);           // gamma-shaped ramp
      const float val    = vmin + (vmax - vmin) * shaped;

      // Near-zero chroma protection (daytime only):
      // As shaped→0, push hue toward pure yellow (~58°) and add sat.
      const float baseHue = daytime ? 48.f  : 300.f;  // yellow / magenta
      const float baseSat = daytime ? 0.45f : 0.20f;  // day more saturated
      const float near    = 1.f - shaped;             // stronger boost near zero
      const float hueEff  = daytime ? (baseHue + (58.f - baseHue) * (0.8f * near)) : baseHue;
      float satEff        = baseSat + (daytime ? 0.30f * near : 0.f);
      satEff = clamp01(satEff);

      col = hsv2rgb(hueEff, satEff, val);
    }

    int idx = seg.reverse ? (end - i) : (start + i);
    strip.setPixelColor(idx, col);
  }
}

void CloudView::addToConfig(JsonObject& subtree) {
  subtree[FPSTR(CFG_SEG_ID)] = segId_;
}

bool CloudView::readFromConfig(JsonObject& subtree,
                               bool startup_complete,
                               bool& invalidate_history) {
  bool configComplete = !subtree.isNull();
  configComplete &= getJsonValue(subtree[FPSTR(CFG_SEG_ID)], segId_, DEFAULT_SEG_ID);
  return configComplete;
}

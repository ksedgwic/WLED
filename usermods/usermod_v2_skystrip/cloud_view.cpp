#include "cloud_view.h"
#include "skymodel.h"
#include "wled.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include "time_util.h"

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

static bool isDay(const SkyModel& m, time_t t) {
  const time_t MAXTT = std::numeric_limits<time_t>::max();
  if (m.sunrise_ == 0 && m.sunset_ == MAXTT) return true;   // 24h day
  if (m.sunset_ == 0 && m.sunrise_ == MAXTT) return false;  // 24h night
  constexpr time_t DAY = 24 * 60 * 60;
  time_t sr = m.sunrise_;
  time_t ss = m.sunset_;
  while (t >= ss) { sr += DAY; ss += DAY; }
  while (t < sr)  { sr -= DAY; ss -= DAY; }
  return t >= sr && t < ss;
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

  const time_t markerTol = time_t(std::llround(step * 0.5));
  const time_t sunrise = model.sunrise_;
  const time_t sunset  = model.sunset_;
  constexpr time_t DAY = 24 * 60 * 60;
  const time_t MAXTT = std::numeric_limits<time_t>::max();

  long offset = time_util::current_offset();

  bool useSunrise = (sunrise != 0 && sunrise != MAXTT);
  bool useSunset  = (sunset  != 0 && sunset  != MAXTT);
  time_t sunriseTOD = 0;
  time_t sunsetTOD  = 0;
  if (useSunrise) sunriseTOD = (sunrise + offset) % DAY;
  if (useSunset)  sunsetTOD  = (sunset  + offset) % DAY;

  auto nearTOD = [&](time_t a, time_t b) {
    time_t diff = (a >= b) ? (a - b) : (b - a);
    if (diff <= markerTol) return true;
    return (DAY - diff) <= markerTol;
  };

  auto isMarker = [&](time_t t) {
    if (!useSunrise && !useSunset) return false;
    time_t tod = (t + offset) % DAY;
    if (useSunrise && nearTOD(tod, sunriseTOD)) return true;
    if (useSunset  && nearTOD(tod, sunsetTOD)) return true;
    return false;
  };

  constexpr float kCloudMaskThreshold = 0.05f;
  constexpr float kDayHue   = 42.f;
  constexpr float kNightHue = 300.f;
  constexpr float kDaySat   = 0.40f;
  constexpr float kNightSat = 0.20f;
  constexpr float kDayVMax  = 0.50f;
  constexpr float kNightVMax= 0.40f;
  constexpr float kMarkerHue= 25.f;
  constexpr float kMarkerSat= 0.60f;
  constexpr float kMarkerVal= 0.50f;

  for (int i = 0; i < len; ++i) {
    const time_t t = now + time_t(std::llround(step * i));
    double clouds, precipTypeVal, precipProb;
    if (!estimateAt(model.cloud_cover_forecast, t, clouds)) continue;
    if (!estimateAt(model.precip_type_forecast, t, precipTypeVal)) precipTypeVal = 0.0;
    if (!estimateAt(model.precip_prob_forecast, t, precipProb)) precipProb = 0.0;

    float clouds01 = clamp01(float(clouds / 100.0));
    int p = int(std::round(precipTypeVal));

    uint32_t col = 0;
    if (isMarker(t)) {
      // always put the sunrise sunset markers in
      col = hsv2rgb(kMarkerHue, kMarkerSat, kMarkerVal);
    } else if (p != 0 && precipProb > 0.0) {
      // precipitation has next priority: rain=blue, snow=white, mixed=cyan-ish
      float ph, ps;
      if (p == 1)      { ph = 210.f; ps = 1.0f; }
      else if (p == 2) { ph = 0.f;   ps = 0.0f; }
      else             { ph = 180.f; ps = 0.5f; }
      float pv = clamp01(float(precipProb));
      pv = 0.3f + 0.7f * pv;
      col = hsv2rgb(ph, ps, pv);
    } else {
      // finally show daytime or nightime clouds
      if (clouds01 < kCloudMaskThreshold) {
        int idx = seg.reverse ? (end - i) : (start + i);
        strip.setPixelColor(idx, 0);
        continue;
      }
      bool daytime = isDay(model, t);
      float vmax = daytime ? kDayVMax : kNightVMax;
      float val = clouds01 * vmax;
      float hue = daytime ? kDayHue : kNightHue;
      float sat = daytime ? kDaySat : kNightSat;
      col = hsv2rgb(hue, sat, val);
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

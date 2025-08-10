#include "delta_view.h"
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
  if (t <= v.front().tstamp) return false;
  if (t >= v.back().tstamp)  return false;
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

// Assumes RGBW32 packs as (W<<24 | R<<16 | G<<8 | B)
static inline uint32_t applySaturation(uint32_t col, float sat) {
  if (sat < 0.f) sat = 0.f; else if (sat > 1.f) sat = 1.f;

  const float r = float((col >> 16) & 0xFF);
  const float g = float((col >>  8) & 0xFF);
  const float b = float((col      ) & 0xFF);

  const float y = 0.2627f*r + 0.6780f*g + 0.0593f*b;

  auto mixc = [&](float c) {
    float v = y + sat * (c - y);
    if (v < 0.f) v = 0.f;
    if (v > 255.f) v = 255.f;
    return v;
  };

  const uint8_t r2 = uint8_t(lrintf(mixc(r)));
  const uint8_t g2 = uint8_t(lrintf(mixc(g)));
  const uint8_t b2 = uint8_t(lrintf(mixc(b)));
  return RGBW32(r2, g2, b2, 0);
}

static inline uint32_t applyIntensity(uint32_t col, float inten) {
  if (inten < 0.f) inten = 0.f; else if (inten > 1.f) inten = 1.f;

  uint8_t r = uint8_t(lrintf(float((col >> 16) & 0xFF) * inten));
  uint8_t g = uint8_t(lrintf(float((col >>  8) & 0xFF) * inten));
  uint8_t b = uint8_t(lrintf(float((col      ) & 0xFF) * inten));
  uint8_t w = uint8_t(lrintf(float((col >> 24) & 0xFF) * inten));
  return RGBW32(r, g, b, w);
}

struct Stop { double f; uint8_t r,g,b; };
// Delta color ramp (\xB0F)
static const Stop kStopsF[] = {
  { -20,   0,   0, 255 }, // very cooling
  { -10,   0, 128, 255 }, // cooling
  {  -5,   0, 255, 255 }, // slight cooling
  {   0,   0, 255,   0 }, // neutral
  {   5, 255, 255,   0 }, // slight warming
  {  10, 255, 128,   0 }, // warming
  {  20, 255,   0,   0 }, // very warming
};

static uint32_t colorForDeltaF(double f) {
  if (f <= kStopsF[0].f) return RGBW32(kStopsF[0].r, kStopsF[0].g, kStopsF[0].b, 0);
  for (size_t i = 1; i < sizeof(kStopsF)/sizeof(kStopsF[0]); ++i) {
    if (f <= kStopsF[i].f) {
      const auto& A = kStopsF[i-1];
      const auto& B = kStopsF[i];
      const double u = (f - A.f) / (B.f - A.f);
      const uint8_t r = uint8_t(std::lround(lerp(A.r, B.r, u)));
      const uint8_t g = uint8_t(std::lround(lerp(A.g, B.g, u)));
      const uint8_t b = uint8_t(std::lround(lerp(A.b, B.b, u)));
      return RGBW32(r,g,b,0);
    }
  }
  const auto& Z = kStopsF[sizeof(kStopsF)/sizeof(kStopsF[0]) - 1];
  return RGBW32(Z.r, Z.g, Z.b, 0);
}

static inline float satFromDewDiffDelta(float delta) {
  constexpr float kMinSat = 0.30f;
  constexpr float kMaxDelta = 15.0f; // +/-15F covers typical range
  float u = clamp01((delta + kMaxDelta) / (2.f * kMaxDelta));
  return kMinSat + (1.f - kMinSat) * u;
}

static inline float intensityFromDeltas(double tempDelta, float humidDelta) {
  constexpr float kMaxTempDelta = 20.0f; // +/-20F covers intensity range
  constexpr float kMaxHumDelta  = 15.0f; // +/-15F covers typical humidity range
  float uT = clamp01(float(std::fabs(tempDelta)) / kMaxTempDelta);
  float uH = clamp01(std::fabs(humidDelta) / kMaxHumDelta);
  return clamp01(std::sqrt(uT*uT + uH*uH));
}

DeltaView::DeltaView()
  : segId_(DEFAULT_SEG_ID) {
  DEBUG_PRINTLN("SkyStrip: DV::CTOR");
}

void DeltaView::view(time_t now, SkyModel const & model) {
  if (segId_ == DEFAULT_SEG_ID) return;
  if (model.temperature_forecast.empty()) return;
  if (segId_ < 0 || segId_ >= strip.getMaxSegments()) return;

  Segment &seg = strip.getSegment((uint8_t)segId_);
  seg.freeze = true;
  int start = seg.start;
  int end   = seg.stop - 1;
  int len   = end - start + 1;
  if (len == 0) return;

  constexpr double kHorizonSec = 48.0 * 3600.0;
  const double step = (len > 1) ? (kHorizonSec / double(len - 1)) : 0.0;
  const time_t day = 24 * 3600;

  for (uint16_t i = 0; i < len; ++i) {
    const time_t t = now + time_t(std::llround(step * i));

    double tempNow, tempPrev;
    if (!estimateAt(model.temperature_forecast, t, tempNow) ||
        !estimateAt(model.temperature_forecast, t - day, tempPrev)) {
      int idx = seg.reverse ? (end - i) : (start + i);
      strip.setPixelColor(idx, 0);
      continue;
    }
    double deltaT = tempNow - tempPrev;

    double dewNow, dewPrev;
    float sat = 1.0f;
    float spreadDelta = 0.f;
    if (estimateAt(model.dew_point_forecast, t, dewNow) &&
        estimateAt(model.dew_point_forecast, t - day, dewPrev)) {
      float spreadNow  = float(tempNow - dewNow);
      float spreadPrev = float(tempPrev - dewPrev);
      spreadDelta = spreadNow - spreadPrev;
      sat = satFromDewDiffDelta(spreadDelta);
    }

    float inten = intensityFromDeltas(deltaT, spreadDelta);
    uint32_t col = colorForDeltaF(deltaT);
    col = applySaturation(col, sat);
    col = applyIntensity(col, inten);

    int idx = seg.reverse ? (end - i) : (start + i);
    strip.setPixelColor(idx, col);
  }
}

void DeltaView::addToConfig(JsonObject& subtree) {
  subtree[FPSTR(CFG_SEG_ID)] = segId_;
}

bool DeltaView::readFromConfig(JsonObject& subtree, bool startup_complete) {
  bool configComplete = !subtree.isNull();
  configComplete &= getJsonValue(subtree[FPSTR(CFG_SEG_ID)], segId_, DEFAULT_SEG_ID);
  return configComplete;
}

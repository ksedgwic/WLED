#include "temperature_view.h"
#include "skymodel.h"
#include "time_util.h"
#include "wled.h"          // Segment, strip, RGBW32
#include <algorithm>
#include <cmath>


static constexpr int16_t DEFAULT_SEG_ID = -1; // -1 means disabled

// - these are user visible in the webapp settings UI
// - they are scoped to this module, don't need to be globally unique
//
const char CFG_SEG_ID[] = "SegmentId";

static inline double lerp(double a, double b, double t) { return a + (b - a) * t; }
template<typename T> static inline T clamp01(T v) {
  return v < T(0) ? T(0) : (v > T(1) ? T(1) : v);
}

const int GRACE_SEC = 60 * 60 * 3; // fencepost + slide
template<class Series>
static bool estimateAt(const Series& v, time_t t, double step, double& out) {
  if (v.empty()) return false;
  // if it's too far away we didn't find estimate
  if (t < v.front().tstamp - GRACE_SEC) return false;
  if (t > v.back().tstamp  + GRACE_SEC) return false;
  // just off the end uses end value
  if (t <= v.front().tstamp) { out = v.front().value; return true; }
  if (t >= v.back().tstamp)  { out = v.back().value;  return true; }
  // otherwise interpolate
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

static bool estimateTempAt(const SkyModel& m, time_t t, double step, double& outF)    {
  return estimateAt(m.temperature_forecast, t, step, outF);
}
static bool estimateDewPtAt(const SkyModel& m, time_t t, double step, double& outFdp) {
  return estimateAt(m.dew_point_forecast, t, step, outFdp);
}

// Assumes RGBW32 packs as (W<<24 | R<<16 | G<<8 | B).
static inline uint8_t Rof(uint32_t c){ return (c >> 16) & 0xFF; }
static inline uint8_t Gof(uint32_t c){ return (c >>  8) & 0xFF; }
static inline uint8_t Bof(uint32_t c){ return (c      ) & 0xFF; }

// Scale saturation by mixing toward luma (keeps perceived brightness stable).
static inline uint32_t applySaturation(uint32_t col, float sat) {
  // sat expected in [0,1]; 0=muggy/gray, 1=full color
  if (sat < 0.f) sat = 0.f; else if (sat > 1.f) sat = 1.f;

  const float r = float((col >> 16) & 0xFF);
  const float g = float((col >>  8) & 0xFF);
  const float b = float((col      ) & 0xFF);

  // Rec.709 luma (linear-ish; good enough here)
  const float y = 0.2627f*r + 0.6780f*g + 0.0593f*b;

  auto mixc = [&](float c) {
    float v = y + sat * (c - y);     // pull toward gray as sat↓
    if (v < 0.f) v = 0.f;
    if (v > 255.f) v = 255.f;
    return v;
  };

  const uint8_t r2 = uint8_t(lrintf(mixc(r)));
  const uint8_t g2 = uint8_t(lrintf(mixc(g)));
  const uint8_t b2 = uint8_t(lrintf(mixc(b)));
  return RGBW32(r2, g2, b2, 0);
}

// Apply a simple brightness scaling.
// "val" expected in [0,1]; 1=no change, 0=black.
static inline uint32_t applyBrightness(uint32_t col, float val) {
  if (val < 0.f) val = 0.f; else if (val > 1.f) val = 1.f;
  const uint8_t r = uint8_t(lrintf(float(Rof(col)) * val));
  const uint8_t g = uint8_t(lrintf(float(Gof(col)) * val));
  const uint8_t b = uint8_t(lrintf(float(Bof(col)) * val));
  return RGBW32(r, g, b, 0);
}

// Map dew-point depression (°F) -> saturation multiplier.
// dd<=2°F  -> minSat ; dd>=25°F -> 1.0 ; smooth in between.
static inline float satFromDewSpreadF(float tempF, float dewF) {
  float dd = tempF - dewF; if (dd < 0.f) dd = 0.f;          // guard bad inputs
  constexpr float kMinSat    = 0.40f;                       // floor (muggy look)
  constexpr float kMaxSpread = 25.0f;                       // “very dry” cap
  float u = clamp01(dd / kMaxSpread);
  float eased = u*u*(3.f - 2.f*u);                          // smoothstep
  return kMinSat + (1.f - kMinSat) * eased;
}

struct Stop { double f; uint8_t r,g,b; };
// Cold→Hot ramp in °F: 14,32,50,68,77,86,95,104
static const Stop kStopsF[] = {
  {  14,  20,  40, 255 }, // deep blue
  {  32,   0, 140, 255 }, // blue/cyan
  {  50,   0, 255, 255 }, // cyan
  {  68,   0, 255,  80 }, // greenish
  {  77, 255, 255,   0 }, // yellow
  {  86, 255, 165,   0 }, // orange
  {  95, 255,  80,   0 }, // orange-red
  { 104, 255,   0,   0 }, // red
};

static uint32_t colorForTempF(double f) {
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

TemperatureView::TemperatureView()
  : segId_(DEFAULT_SEG_ID) {
  DEBUG_PRINTLN("SkyStrip: TV::CTOR");
}

void TemperatureView::view(time_t now, SkyModel const & model, int16_t dbgPixelIndex) {
  if (segId_ == DEFAULT_SEG_ID) return;                      // disabled
  if (model.temperature_forecast.empty()) return;            // nothing to render

  if (segId_ < 0 || segId_ >= strip.getMaxSegments()) return;
  Segment &seg = strip.getSegment((uint8_t)segId_);
  seg.freeze = true;
  int  start = seg.start;
  int    end = seg.stop - 1;    // inclusive
  int    len = end - start + 1;
  if (len == 0) return;

  constexpr double kHorizonSec = 48.0 * 3600.0;
  const double step = (len > 1) ? (kHorizonSec / double(len - 1)) : 0.0;
  constexpr time_t DAY = 24 * 60 * 60;
  const long tzOffset = time_util::current_offset();

  // Returns [0,1] marker weight based on proximity to midnight/noon boundaries
  // in local time.
  auto markerWeight = [&](time_t t) {
    if (step <= 0.0) return 0.f;
    time_t local = t + tzOffset;                    // convert to local seconds
    time_t s = local % DAY; if (s < 0) s += DAY;
    time_t diffMid  = (s <= DAY/2) ? s : DAY - s;
    time_t diffNoon = (s >= DAY/2) ? s - DAY/2 : DAY/2 - s;
    time_t diff = (diffMid < diffNoon) ? diffMid : diffNoon;
    float w = 1.f - float(diff) / float(step); // 1 at center, 0 one pixel away
    return (w > 0.f) ? w : 0.f;
  };

  for (uint16_t i = 0; i < len; ++i) {
    const time_t t = now + time_t(std::llround(step * i));
    int idx = seg.reverse ? (end - i) : (start + i);

    double tempF = 0.f;
    double dewF = 0.f;
    uint32_t col = 0;
    float sat = 1.0f;
    if (estimateTempAt(model, t, step, tempF)) {
      col = colorForTempF(tempF);

      if (estimateDewPtAt(model, t, step, dewF)) {
        sat = satFromDewSpreadF((float)tempF, (float)dewF);
      }
      col = applySaturation(col, sat);
      col = applyBrightness(col, 0.7f);
    }

    float m = markerWeight(t);
    if (m > 0.f) {
      uint8_t blend = uint8_t(std::lround(m * 255.f));
      col = color_blend(col, 0, blend);
    }

    if (dbgPixelIndex >= 0) {
      static time_t lastDebug = 0;
      if (now - lastDebug > 30 && i == dbgPixelIndex) {
        char tmbuf0[20];
        time_util::fmt_local(tmbuf0, sizeof(tmbuf0), t);
        DEBUG_PRINTF("SkyStrip: TV: i=%u                      timeNow=%s                     T=%.1fF           D=%.1fF sat=%.2f col=%08x\n",
                     i, tmbuf0, tempF, dewF, sat, (unsigned)col);
        lastDebug = now;
      }
    }

    strip.setPixelColor(idx, col);
  }
}

void TemperatureView::addToConfig(JsonObject& subtree) {
  subtree[FPSTR(CFG_SEG_ID)] = segId_;
}

bool TemperatureView::readFromConfig(JsonObject& subtree,
                                     bool startup_complete,
                                     bool& invalidate_history) {
  bool configComplete = !subtree.isNull();
  configComplete &= getJsonValue(subtree[FPSTR(CFG_SEG_ID)], segId_, DEFAULT_SEG_ID);
  return configComplete;
}


#include <algorithm>
#include <cmath>

#include "delta_view.h"
#include "skymodel.h"
#include "wled.h"
#include "util.h"

static constexpr int16_t DEFAULT_SEG_ID = -1; // -1 means disabled
const char CFG_SEG_ID[] = "SegmentId";


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
      const uint8_t r = uint8_t(std::lround(util::lerp(A.r, B.r, u)));
      const uint8_t g = uint8_t(std::lround(util::lerp(A.g, B.g, u)));
      const uint8_t b = uint8_t(std::lround(util::lerp(A.b, B.b, u)));
      return RGBW32(r,g,b,0);
    }
  }
  const auto& Z = kStopsF[sizeof(kStopsF)/sizeof(kStopsF[0]) - 1];
  return RGBW32(Z.r, Z.g, Z.b, 0);
}

static inline float satFromDewDiffDelta(float delta) {
  constexpr float kMinSat = 0.30f;
  constexpr float kMaxDelta = 15.0f; // +/-15F covers typical range
  float u = util::clamp01((delta + kMaxDelta) / (2.f * kMaxDelta));
  return kMinSat + (1.f - kMinSat) * u;
}

static inline float intensityFromDeltas(double tempDelta, float humidDelta) {
  constexpr float kMaxTempDelta = 20.0f; // +/-20F covers intensity range
  constexpr float kMaxHumDelta  = 15.0f; // +/-15F covers typical humidity range
  float uT = util::clamp01(float(std::fabs(tempDelta)) / kMaxTempDelta);
  float uH = util::clamp01(std::fabs(humidDelta) / kMaxHumDelta);
  return util::clamp01(std::sqrt(uT*uT + uH*uH)) * 0.7;
}

DeltaView::DeltaView()
  : segId_(DEFAULT_SEG_ID) {
  DEBUG_PRINTLN("SkyStrip: DV::CTOR");
  snprintf(debugPixelString, sizeof(debugPixelString), "%s:\\n", name().c_str());
  debugPixelString[sizeof(debugPixelString) - 1] = '\0';
}

void DeltaView::view(time_t now, SkyModel const & model, int16_t dbgPixelIndex) {
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
    int idx = seg.reverse ? (end - i) : (start + i);

    double tempNow, tempPrev;
    bool foundTempNow = util::estimateAt(model.temperature_forecast, t, step, tempNow);
    bool foundTempPrev = util::estimateAt(model.temperature_forecast, t - day, step, tempPrev);

    if (!foundTempNow || !foundTempPrev) {
      if (dbgPixelIndex >= 0) {
        static time_t lastDebug = 0;
        if (now - lastDebug > 1 && i == dbgPixelIndex) {
          char nowbuf[20];
          util::fmt_local(nowbuf, sizeof(nowbuf), now);
          char dbgbuf[20];
          util::fmt_local(dbgbuf, sizeof(dbgbuf), t);
          char prvbuf[20];
          util::fmt_local(prvbuf, sizeof(prvbuf), t - day);
          snprintf(debugPixelString, sizeof(debugPixelString),
                   "%s: nowtm=%s dbgndx=%d dbgtm=%s prvtm=%s "
                   "foundTempPrev=%d foundTempNow=%d\\n",
                   name().c_str(), nowbuf, i, dbgbuf, prvbuf,
                   foundTempPrev, foundTempNow);
          lastDebug = now;
        }
      }
      strip.setPixelColor(idx, 0);
      continue;
    }
    double deltaT = tempNow - tempPrev;

    double dewNow, dewPrev;
    float sat = 1.0f;
    float spreadDelta = 0.f;
    if (util::estimateAt(model.dew_point_forecast, t, step, dewNow) &&
        util::estimateAt(model.dew_point_forecast, t - day, step, dewPrev)) {
      float spreadNow  = float(tempNow - dewNow);
      float spreadPrev = float(tempPrev - dewPrev);
      spreadDelta = spreadNow - spreadPrev;
      sat = satFromDewDiffDelta(spreadDelta);
    }

    float inten = intensityFromDeltas(deltaT, spreadDelta);
    uint32_t col = colorForDeltaF(deltaT);
    col = util::applySaturation(col, sat);
    col = applyIntensity(col, inten);

    if (dbgPixelIndex >= 0) {
      static time_t lastDebug = 0;
      if (now - lastDebug > 1 && i == dbgPixelIndex) {
        char nowbuf[20];
        util::fmt_local(nowbuf, sizeof(nowbuf), now);
        char dbgbuf[20];
        util::fmt_local(dbgbuf, sizeof(dbgbuf), t);
        char prvbuf[20];
        util::fmt_local(prvbuf, sizeof(prvbuf), t - day);
        snprintf(debugPixelString, sizeof(debugPixelString),
                 "%s: nowtm=%s dbgndx=%d dbgtm=%s prvtm=%s "
                 "tempDbg=%.1f dewDbg=%.1f tempPrev=%.1f dewPrev=%.1f "
                 "sat=%.2f col=%08x\\n",
                 name().c_str(), nowbuf, i, dbgbuf, prvbuf,
                 tempNow, dewNow, tempPrev, dewPrev,
                 sat*100, (unsigned) col);
        lastDebug = now;
      }
    }

    strip.setPixelColor(idx, col);
  }
}

void DeltaView::addToConfig(JsonObject& subtree) {
  subtree[FPSTR(CFG_SEG_ID)] = segId_;
}

void DeltaView::appendConfigData(Print& s) {
  // Keep the hint INLINE (BEFORE the input = 4th arg):
  s.print(F(
    "addInfo('SkyStrip:DeltaView:SegmentId',1,'',"
    "'&nbsp;<small style=\\'opacity:.8\\'>(-1 disables)</small>'"
    ");"
  ));
}

bool DeltaView::readFromConfig(JsonObject& subtree,
                               bool startup_complete,
                               bool& invalidate_history) {
  bool configComplete = !subtree.isNull();
  configComplete &= getJsonValue(subtree[FPSTR(CFG_SEG_ID)], segId_, DEFAULT_SEG_ID);
  return configComplete;
}

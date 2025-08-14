#include "test_pattern_view.h"
#include "skymodel.h"
#include "wled.h"
#include <cmath>

static constexpr int16_t DEFAULT_SEG_ID = -1; // -1 means disabled
const char CFG_SEG_ID[] = "SegmentId";
const char CFG_START_HUE[] = "StartHue";
const char CFG_START_SAT[] = "StartSat";
const char CFG_START_VAL[] = "StartVal";
const char CFG_END_HUE[]   = "EndHue";
const char CFG_END_SAT[]   = "EndSat";
const char CFG_END_VAL[]   = "EndVal";

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

TestPatternView::TestPatternView()
  : segId_(DEFAULT_SEG_ID),
    startHue_(0.f), startSat_(0.f), startVal_(0.f),
    endHue_(0.f), endSat_(0.f), endVal_(1.f) {
  DEBUG_PRINTLN("SkyStrip: TP::CTOR");
}

void TestPatternView::view(time_t now, SkyModel const & model, int16_t dbgPixelIndex) {
  if (segId_ == DEFAULT_SEG_ID) return;
  if (segId_ < 0 || segId_ >= strip.getMaxSegments()) return;

  Segment &seg = strip.getSegment((uint8_t)segId_);
  seg.freeze = true;
  int start = seg.start;
  int end = seg.stop - 1;
  int len = end - start + 1;
  if (len == 0) return;

  for (int i = 0; i < len; ++i) {
    float u = (len > 1) ? float(i) / float(len - 1) : 0.f;
    float h = startHue_ + (endHue_ - startHue_) * u;
    float s = startSat_ + (endSat_ - startSat_) * u;
    float v = startVal_ + (endVal_ - startVal_) * u;
    uint32_t col = hsv2rgb(h, s, v);
    int idx = seg.reverse ? (end - i) : (start + i);
    strip.setPixelColor(idx, col);
  }
}

void TestPatternView::addToConfig(JsonObject& subtree) {
  subtree[FPSTR(CFG_SEG_ID)] = segId_;
  subtree[FPSTR(CFG_START_HUE)] = startHue_;
  subtree[FPSTR(CFG_START_SAT)] = startSat_;
  subtree[FPSTR(CFG_START_VAL)] = startVal_;
  subtree[FPSTR(CFG_END_HUE)] = endHue_;
  subtree[FPSTR(CFG_END_SAT)] = endSat_;
  subtree[FPSTR(CFG_END_VAL)] = endVal_;
}

bool TestPatternView::readFromConfig(JsonObject& subtree,
                                     bool startup_complete,
                                     bool& invalidate_history) {
  bool configComplete = !subtree.isNull();
  configComplete &= getJsonValue(subtree[FPSTR(CFG_SEG_ID)], segId_, DEFAULT_SEG_ID);
  configComplete &= getJsonValue(subtree[FPSTR(CFG_START_HUE)], startHue_, 0.f);
  configComplete &= getJsonValue(subtree[FPSTR(CFG_START_SAT)], startSat_, 0.f);
  configComplete &= getJsonValue(subtree[FPSTR(CFG_START_VAL)], startVal_, 0.f);
  configComplete &= getJsonValue(subtree[FPSTR(CFG_END_HUE)], endHue_, 0.f);
  configComplete &= getJsonValue(subtree[FPSTR(CFG_END_SAT)], endSat_, 0.f);
  configComplete &= getJsonValue(subtree[FPSTR(CFG_END_VAL)], endVal_, 1.f);
  return configComplete;
}

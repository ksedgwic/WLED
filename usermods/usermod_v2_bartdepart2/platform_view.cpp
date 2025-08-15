#include "platform_view.h"
#include "wled.h"

// helper to map TrainColor enum → CRGB
static CRGB colorFromTrainColor(TrainColor tc) {
  switch(tc) {
    case TrainColor::Red:    return CRGB(255,  0,  0);
    case TrainColor::Orange: return CRGB(255,150, 30);
    case TrainColor::Yellow: return CRGB(255,255,  0);
    case TrainColor::Green:  return CRGB(  0,255,  0);
    case TrainColor::Blue:   return CRGB(  0,  0,255);
    case TrainColor::White:  return CRGB(255,255,255);
    default:                 return CRGB(  0,  0,  0);
  }
}

void PlatformView::view(std::time_t now, const BartStationModel& model, int16_t dbgPixelIndex) {
  for (const auto& platform : model.platforms) {
    if (platform.platformId() != platformId_) continue;

    const auto& history = platform.history();
    if (platform.platformId().isEmpty()) return;
    if (history.empty()) return;

    static uint8_t frameCnt = 0;           // 0..99
    bool preferFirst = frameCnt < 50;      // true for first 0.5 s
    frameCnt = (frameCnt + 1) % 100;

    const auto& batch = history.back();

    auto &seg = strip.getSegment(segmentId_);
    seg.freeze = true;
    int  start = seg.start;
    int    end = seg.stop - 1;    // inclusive
    int    len = end - start + 1;

    for (int i = start; i <= end; i++) {
      strip.setPixelColor(i, 0);
    }

    int base  = seg.reverse ? end : start;
    int dir   = seg.reverse ? -1  : +1;

    for (auto &e : batch.etds) {
      float diffMin = float(updateSecs_ + e.estDep - now) / 60.0f;
      if (diffMin < 0 || diffMin >= len) continue;

      int   idx  = int(floor(diffMin));
      float frac = diffMin - float(idx);
      uint8_t b1 = uint8_t((1.0f - frac) * 255);
      uint8_t b2 = uint8_t(frac * 255);

      CRGB col = colorFromTrainColor(e.color);

      int pos1 = base + dir*idx;
      int pos2 = base + dir*(idx+1);

      auto brightness = [&](uint32_t c) {
        return (uint16_t)(((c >> 16) & 0xFF) + ((c >> 8) & 0xFF) + (c & 0xFF));
      };

      if (pos1 >= start && pos1 <= end) {
        uint32_t existing = strip.getPixelColor(pos1);
        uint32_t newColor1 =
          (((uint32_t)col.r * b1 / 255) << 16) |
          (((uint32_t)col.g * b1 / 255) <<  8) |
           ((uint32_t)col.b * b1 / 255);

        uint16_t oldB = brightness(existing);
        uint16_t newB = brightness(newColor1);

        if ((preferFirst && (existing == 0 || newB > 2*oldB))
            || (!preferFirst && newB * 2 >= oldB)) {
          strip.setPixelColor(pos1, newColor1);
        }
      }

      if (pos2 >= start && pos2 <= end) {
        uint32_t existing2 = strip.getPixelColor(pos2);
        uint32_t newColor2 =
          (((uint32_t)col.r * b2 / 255) << 16) |
          (((uint32_t)col.g * b2 / 255) <<  8) |
           ((uint32_t)col.b * b2 / 255);

        uint16_t oldB2 = brightness(existing2);
        uint16_t newB2 = brightness(newColor2);
        if ((preferFirst && (existing2 == 0 || newB2 > 2*oldB2))
            || (!preferFirst && newB2 * 2 >= oldB2)) {
          strip.setPixelColor(pos2, newColor2);
        }
      }
    }
    break;
  }
}

#include "platform_view.h"
#include "util.h"
#include "wled.h"

// helper to map TrainColor enum → CRGB
static CRGB colorFromTrainColor(TrainColor tc) {
  switch (tc) {
  case TrainColor::Red:
    return CRGB(255, 0, 0);
  case TrainColor::Orange:
    return CRGB(255, 150, 30);
  case TrainColor::Yellow:
    return CRGB(255, 255, 0);
  case TrainColor::Green:
    return CRGB(0, 255, 0);
  case TrainColor::Blue:
    return CRGB(0, 0, 255);
  case TrainColor::White:
    return CRGB(255, 255, 255);
  default:
    return CRGB(0, 0, 0);
  }
}

void PlatformView::view(std::time_t now, const BartStationModel &model,
                        int16_t dbgPixelIndex) {
  if (segmentId_ == -1)
    return;
  for (const auto &platform : model.platforms) {
    if (platform.platformId() != platformId_)
      continue;

    const auto &history = platform.history();
    if (platform.platformId().isEmpty())
      return;
    if (history.empty())
      return;

    static uint8_t frameCnt = 0;      // 0..99
    bool preferFirst = frameCnt < 50; // true for first 0.5 s
    frameCnt = (frameCnt + 1) % 100;

    const auto &batch = history.back();

    auto &seg = strip.getSegment(segmentId_);
    seg.freeze = true;
    int start = seg.start;
    int end = seg.stop - 1; // inclusive
    int len = end - start + 1;

    for (int i = start; i <= end; i++) {
      strip.setPixelColor(i, 0);
    }

    int base = seg.reverse ? end : start;
    int dir = seg.reverse ? -1 : +1;

    auto brightness = [&](uint32_t c) {
      return (uint16_t)(((c >> 16) & 0xFF) + ((c >> 8) & 0xFF) + (c & 0xFF));
    };

    for (int i = 0; i < len; ++i) {
      uint32_t bestColor = 0;
      uint16_t bestB = 0;

      for (auto &e : batch.etds) {
        float diffMin = float(updateSecs_ + e.estDep - now) / 60.0f;
        if (diffMin < 0 || diffMin >= len)
          continue;

        int idx = int(floor(diffMin));
        float frac = diffMin - float(idx);

        uint8_t b = 0;
        if (i == idx) {
          b = uint8_t((1.0f - frac) * 255);
        } else if (i == idx + 1) {
          b = uint8_t(frac * 255);
        } else {
          continue;
        }

        CRGB col = colorFromTrainColor(e.color);
        uint32_t newColor = (((uint32_t)col.r * b / 255) << 16) |
                            (((uint32_t)col.g * b / 255) << 8) |
                            ((uint32_t)col.b * b / 255);

        uint16_t newB = brightness(newColor);
        if ((preferFirst && (bestColor == 0 || newB > 2 * bestB)) ||
            (!preferFirst && newB * 2 >= bestB)) {
          bestColor = newColor;
          bestB = newB;
        }
      }

      int pos = base + dir * i;
      strip.setPixelColor(pos, util::blinkDebug(i, dbgPixelIndex, bestColor));
    }
    break;
  }
}

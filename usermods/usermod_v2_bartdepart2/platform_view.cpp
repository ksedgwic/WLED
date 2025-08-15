#include "platform_view.h"

void PlatformView::view(std::time_t now, const BartStationModel& model, int16_t dbgPixelIndex) {
  for (const auto& platform : model.platforms) {
    if (platform.platformId() == platformId_) {
      platform.display(now, segmentId_, updateSecs_);
      break;
    }
  }
}

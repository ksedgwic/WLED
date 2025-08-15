#include "bart_depart_view.h"

void BartDepartView::view(std::time_t now, const BartModel& model, int16_t dbgPixelIndex) {
  size_t segment = 1;
  for (const auto& platform : model.platforms) {
    platform.display(now, segment++, updateSecs_);
  }
}

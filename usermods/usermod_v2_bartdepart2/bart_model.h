#pragma once

#include <vector>
#include <algorithm>
#include "train_platform_model.h"

struct BartModel {
  std::vector<TrainPlatformModel> platforms;

  void update(std::time_t now, BartModel&& delta) {
    for (auto &p : delta.platforms) {
      auto it = std::find_if(platforms.begin(), platforms.end(),
        [&](const TrainPlatformModel& x){ return x.platformId() == p.platformId(); });
      if (it != platforms.end()) {
        it->merge(p);
      } else {
        platforms.push_back(std::move(p));
      }
    }
  }

  time_t oldest() const {
    time_t oldest = 0;
    for (auto const& p : platforms) {
      time_t o = p.oldest();
      if (!oldest || (o && o < oldest)) oldest = o;
    }
    return oldest;
  }
};

#pragma once

#include <ctime>
#include <memory>

#include "interfaces.h"

struct DataPoint {
  time_t tstamp;
  double value;
};

class SkyModel {
public:
  SkyModel() = default;

  // move-only
  SkyModel(const SkyModel &) = delete;
  SkyModel &operator=(const SkyModel &) = delete;

  SkyModel(SkyModel &&) noexcept = default;
  SkyModel &operator=(SkyModel &&) noexcept = default;

  ~SkyModel() = default;

  SkyModel & update(time_t now, SkyModel && other);  // use std::move

  String toString(time_t now) const;

  std::time_t lcl_tstamp{0};			// update timestamp from our clock
  std::vector<DataPoint> temperature_forecast;
};

#pragma once

#include <ctime>
#include <memory>
#include <deque>

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
  void invalidate_history(time_t now);
  String toString(time_t now) const;
  time_t oldest() const;

  std::time_t lcl_tstamp{0};			// update timestamp from our clock
  std::deque<DataPoint> temperature_forecast;
  std::deque<DataPoint> dew_point_forecast;
  std::deque<DataPoint> wind_speed_forecast;
  std::deque<DataPoint> wind_gust_forecast;
  std::deque<DataPoint> wind_dir_forecast;
  std::deque<DataPoint> cloud_cover_forecast;
  std::deque<DataPoint> precip_type_forecast;   // 0 none, 1 rain, 2 snow, 3 mixed
  std::deque<DataPoint> precip_prob_forecast;   // 0..1 probability of precip

  // sunrise/sunset times from current data
  time_t sunrise_{0};
  time_t sunset_{0};
};

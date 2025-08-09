#pragma once

#include <ctime>
#include <memory>
#include <vector>

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
  std::vector<DataPoint> dew_point_forecast;
  std::vector<DataPoint> wind_speed_forecast;
  std::vector<DataPoint> wind_dir_forecast;
  std::vector<DataPoint> wind_gust_forecast;
  std::vector<DataPoint> cloud_cover_forecast;
  std::vector<DataPoint> daylight_forecast;      // 1=day, 0=night
  std::vector<DataPoint> precip_prob_forecast;   // 0..1 probability of precip
  std::vector<DataPoint> precip_type_forecast;   // 0 none, 1 rain, 2 snow, 3 mixed
};

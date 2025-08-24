#include <cassert>
#include <limits>
#include <algorithm>

#include "wled.h"

#include "skymodel.h"
#include "util.h"

namespace {
  static constexpr time_t HISTORY_SEC = 25 * 60 * 60;  // keep an extra history point
  // Preallocate enough space for forecast (48h) plus backfilled history (~24h)
  // without imposing a hard cap; vectors can still grow beyond this reserve.
  static constexpr size_t MAX_POINTS = 80;

template <class Series>
void mergeSeries(Series &current, Series &&fresh, time_t now) {
  if (fresh.empty()) return;

  if (current.empty()) {
    current = std::move(fresh);
  } else if (fresh.back().tstamp < current.front().tstamp) {
    // Fresh points are entirely earlier than current data; prepend in-place.
    fresh.reserve(current.size() + fresh.size());
    fresh.insert(fresh.end(), current.begin(), current.end());
    current = std::move(fresh);
  } else {
    auto it = std::lower_bound(current.begin(), current.end(), fresh.front().tstamp,
                               [](const DataPoint& dp, time_t t){ return dp.tstamp < t; });
    current.erase(it, current.end());
    current.insert(current.end(), fresh.begin(), fresh.end());
  }

  time_t cutoff = now - HISTORY_SEC;
  auto itCut = std::lower_bound(current.begin(), current.end(), cutoff,
                                [](const DataPoint& dp, time_t t){ return dp.tstamp < t; });
  current.erase(current.begin(), itCut);
}
} // namespace

SkyModel::SkyModel() {
  temperature_forecast.reserve(MAX_POINTS);
  dew_point_forecast.reserve(MAX_POINTS);
  wind_speed_forecast.reserve(MAX_POINTS);
  wind_gust_forecast.reserve(MAX_POINTS);
  wind_dir_forecast.reserve(MAX_POINTS);
  cloud_cover_forecast.reserve(MAX_POINTS);
  precip_type_forecast.reserve(MAX_POINTS);
  precip_prob_forecast.reserve(MAX_POINTS);
}

SkyModel & SkyModel::update(time_t now, SkyModel && other) {
  lcl_tstamp = other.lcl_tstamp;

  mergeSeries(temperature_forecast, std::move(other.temperature_forecast), now);
  mergeSeries(dew_point_forecast, std::move(other.dew_point_forecast), now);
  mergeSeries(wind_speed_forecast, std::move(other.wind_speed_forecast), now);
  mergeSeries(wind_gust_forecast, std::move(other.wind_gust_forecast), now);
  mergeSeries(wind_dir_forecast, std::move(other.wind_dir_forecast), now);
  mergeSeries(cloud_cover_forecast, std::move(other.cloud_cover_forecast), now);
  mergeSeries(precip_type_forecast, std::move(other.precip_type_forecast), now);
  mergeSeries(precip_prob_forecast, std::move(other.precip_prob_forecast), now);

  if (!(other.sunrise_ == 0 && other.sunset_ == 0)) {
    sunrise_ = other.sunrise_;
    sunset_  = other.sunset_;
  }

  emitDebug(now, [](const String& line){
    DEBUG_PRINTF("%s\n", line.c_str());
  });
 
  return *this;
}

void SkyModel::invalidate_history(time_t now) {
  temperature_forecast.clear();
  dew_point_forecast.clear();
  wind_speed_forecast.clear();
  wind_gust_forecast.clear();
  wind_dir_forecast.clear();
  cloud_cover_forecast.clear();
  precip_type_forecast.clear();
  precip_prob_forecast.clear();
  sunrise_ = 0;
  sunset_  = 0;
}

time_t SkyModel::oldest() const {
  time_t out = std::numeric_limits<time_t>::max();
  auto upd = [&](const std::vector<DataPoint>& s){
    if (!s.empty() && s.front().tstamp < out) out = s.front().tstamp;
  };
  upd(temperature_forecast);
  upd(dew_point_forecast);
  upd(wind_speed_forecast);
  upd(wind_gust_forecast);
  upd(wind_dir_forecast);
  upd(cloud_cover_forecast);
  upd(precip_type_forecast);
  upd(precip_prob_forecast);
  if (out == std::numeric_limits<time_t>::max()) return 0;
  return out;
}

// Streamed/line-by-line variant to keep packets small.
template <class Series>
static inline void emitSeriesMDHM(const std::function<void(const String&)> &emit,
                                  time_t /*now*/,
                                  const __FlashStringHelper *label,
                                  const Series &s) {
  // Header
  {
    String line;
    line.reserve(64);
    line += F("SkyModel: ");
    line += label;
    line += F("(");
    line += String(s.size());
    line += F("):[");
    emit(line);
  }

  if (s.empty()) {
    emit(String(F("SkyModel: ]")));
    return;
  }

  char tb[20];
  char valbuf[16];
  size_t i = 0;
  String line;
  line.reserve(256);
  for (const auto& dp : s) {
    if (i % 6 == 0) {
      if (line.length()) { emit(line); line = ""; }
      line += F("SkyModel: ");
    }
    util::fmt_local(tb, sizeof(tb), dp.tstamp);
    snprintf(valbuf, sizeof(valbuf), "%6.2f", dp.value);
    line += F(" (");
    line += tb;
    line += F(", ");
    line += valbuf;
    line += F(")");
    if (i % 6 == 5 || i == s.size() - 1) {
      if (i == s.size() - 1) line += F(" ]");
      emit(line);
      line = "";
    }
    ++i;
  }
}

void SkyModel::emitDebug(time_t now, const std::function<void(const String&)> &emit) const {
  emitSeriesMDHM(emit, now, F(" temp"),  temperature_forecast);
  emitSeriesMDHM(emit, now, F(" dwpt"),  dew_point_forecast);
  emitSeriesMDHM(emit, now, F(" wspd"),  wind_speed_forecast);
  emitSeriesMDHM(emit, now, F(" wgst"),  wind_gust_forecast);
  emitSeriesMDHM(emit, now, F(" wdir"),  wind_dir_forecast);
  emitSeriesMDHM(emit, now, F(" clds"),  cloud_cover_forecast);
  emitSeriesMDHM(emit, now, F(" prcp"),  precip_type_forecast);
  emitSeriesMDHM(emit, now, F(" pop"),   precip_prob_forecast);

  // Sunrise / Sunset as separate small lines
  {
    char tb[20];
    String line;
    line.reserve(64);
    util::fmt_local(tb, sizeof(tb), sunrise_);
    line  = F("SkyModel: sunrise "); line += tb; emit(line);
    util::fmt_local(tb, sizeof(tb), sunset_);
    line  = F("SkyModel: sunset ");  line += tb; emit(line);
  }
}

#include <cassert>
#include <limits>

#include "wled.h"

#include "skymodel.h"
#include "time_util.h"

namespace {
  static constexpr time_t HISTORY_SEC = 25 * 60 * 60;  // keep an extra history point

template <class Series>
void mergeSeries(Series &current, Series &&fresh, time_t now) {
  Series merged;

  if (fresh.empty()) {
    merged = current;
  } else if (current.empty()) {
    merged = std::move(fresh);
  } else if (fresh.back().tstamp < current.front().tstamp) {
    // incoming data is entirely older than what we have, prepend it
    merged = std::move(fresh);
    merged.insert(merged.end(), current.begin(), current.end());
  } else {
    // incoming data is current or newer, keep older history
    merged = std::move(fresh);
    time_t earliest_new = merged.front().tstamp;

    Series older;
    for (const auto &dp : current) {
      if (dp.tstamp < earliest_new) older.push_back(dp);
      else break;
    }
    merged.insert(merged.begin(), older.begin(), older.end());
  }

  time_t cutoff = now - HISTORY_SEC;
  while (!merged.empty() && merged.front().tstamp < cutoff) {
    merged.pop_front();
  }
  current = std::move(merged);
}
} // namespace

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
  auto upd = [&](const std::deque<DataPoint>& s){
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
      if (line.length()) { emit(line); line = String(); line.reserve(256); }
      line += F("SkyModel: ");
    }
    time_util::fmt_local(tb, sizeof(tb), dp.tstamp);
    snprintf(valbuf, sizeof(valbuf), "%6.2f", dp.value);
    line += F(" (");
    line += tb;
    line += F(", ");
    line += valbuf;
    line += F(")");
    if (i % 6 == 5 || i == s.size() - 1) {
      if (i == s.size() - 1) line += F(" ]");
      emit(line);
      line = String();
      line.reserve(256);
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
    time_util::fmt_local(tb, sizeof(tb), sunrise_);
    line  = F("SkyModel: sunrise "); line += tb; emit(line);
    time_util::fmt_local(tb, sizeof(tb), sunset_);
    line  = F("SkyModel: sunset ");  line += tb; emit(line);
  }
}

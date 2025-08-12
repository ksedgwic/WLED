#include <cassert>
#include <limits>

#include "wled.h"

#include "skymodel.h"
#include "time_util.h"

namespace {
static constexpr time_t HISTORY_SEC = 24 * 60 * 60;

template <class Series>
void mergeSeries(Series &current, Series &&fresh, time_t now) {
  Series merged = std::move(fresh);
  time_t earliest_new = merged.empty()
                          ? std::numeric_limits<time_t>::max()
                          : merged.front().tstamp;

  Series older;
  for (const auto &dp : current) {
    if (dp.tstamp < earliest_new) older.push_back(dp);
    else break;
  }
  merged.insert(merged.begin(), older.begin(), older.end());

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

  sunrise_ = other.sunrise_;
  sunset_  = other.sunset_;

  char nowBuf[20];
  time_util::fmt_local(nowBuf, sizeof(nowBuf), now);
  DEBUG_PRINTF("SkyStrip: SkyModel::update: %s\n%s", nowBuf, toString(now).c_str());

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

template <class Series>
static inline void appendSeriesMDHM(String &out, time_t now,
                                    const __FlashStringHelper *label,
                                    const Series &s) {
  out += F("SkyModel: ");
  out += label;
  out += F("(");
  out += String(s.size());
  out += F("):[\n");

  if (s.empty()) {
    out += F("SkyModel: ]\n");
    return;
  }

  char tb[20];
  char valbuf[16];
  size_t i = 0;
  for (const auto& dp : s) {
    if (i % 6 == 0) {
      out += F("SkyModel: ");
    }
    time_util::fmt_local(tb, sizeof(tb), dp.tstamp);
    snprintf(valbuf, sizeof(valbuf), "%6.2f", dp.value);
    out += F(" (");
    out += tb;
    out += F(", ");
    out += valbuf;
    out += F(")");
    if (i % 6 == 5 || i == s.size() - 1) {
      if (i == s.size() - 1) out += F(" ]\n");
      else out += F("\n");
    }
    ++i;
  }
}


String SkyModel::toString(time_t now) const {
  String out;
  appendSeriesMDHM(out, now, F(" temp"), temperature_forecast);
  appendSeriesMDHM(out, now, F(" dwpt"), dew_point_forecast);
  appendSeriesMDHM(out, now, F(" wspd"), wind_speed_forecast);
  appendSeriesMDHM(out, now, F(" wgst"), wind_gust_forecast);
  appendSeriesMDHM(out, now, F(" wdir"), wind_dir_forecast);
  appendSeriesMDHM(out, now, F(" clds"), cloud_cover_forecast);
  appendSeriesMDHM(out, now, F(" prcp"), precip_type_forecast);
  appendSeriesMDHM(out, now, F(" pop"), precip_prob_forecast);

  char tb[20];
  time_util::fmt_local(tb, sizeof(tb), sunrise_);
  out += F("SkyModel: sunrise ");
  out += tb;
  out += F("\n");
  time_util::fmt_local(tb, sizeof(tb), sunset_);
  out += F("SkyModel: sunset ");
  out += tb;
  out += F("\n");
  return out;
}

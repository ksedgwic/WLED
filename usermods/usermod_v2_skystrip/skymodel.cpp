#include <cassert>

#include "wled.h"

#include "skymodel.h"
#include "time_util.h"

SkyModel & SkyModel::update(time_t now, SkyModel && other) {
  lcl_tstamp = other.lcl_tstamp;

  if (!other.temperature_forecast.empty())
    temperature_forecast.swap(other.temperature_forecast);
  if (!other.dew_point_forecast.empty())
    dew_point_forecast.swap(other.dew_point_forecast);
  if (!other.wind_speed_forecast.empty())
    wind_speed_forecast.swap(other.wind_speed_forecast);
  if (!other.wind_dir_forecast.empty())
    wind_dir_forecast.swap(other.wind_dir_forecast);
  if (!other.wind_gust_forecast.empty())
    wind_gust_forecast.swap(other.wind_gust_forecast);
  if (!other.cloud_cover_forecast.empty())
    cloud_cover_forecast.swap(other.cloud_cover_forecast);
  if (!other.daylight_forecast.empty())
    daylight_forecast.swap(other.daylight_forecast);
  if (!other.precip_prob_forecast.empty())
    precip_prob_forecast.swap(other.precip_prob_forecast);
  if (!other.precip_type_forecast.empty())
    precip_type_forecast.swap(other.precip_type_forecast);

  char nowBuf[20];
  time_util::fmt_local(nowBuf, sizeof(nowBuf), now);
  DEBUG_PRINTF("SkyStrip: SkyModel::update: %s\n%s", nowBuf, toString(now).c_str());

  return *this;
}

template <class Series>
static inline void appendSeriesMDHM(String &out, time_t now,
                                    const __FlashStringHelper *label,
                                    const Series &s) {
  out += F("SkyModel: ");
  out += label;
  out += F("(");
  out += String(s.size());
  out += F("):[");

  char tb[20];
  for (const auto& dp : s) {
    time_util::fmt_local(tb, sizeof(tb), dp.tstamp);
    out += F(" (");
    out += tb;
    out += F(", ");
    out += String(dp.value, 2);
    out += F(")");
  }
  out += F(" ]\n");
}


String SkyModel::toString(time_t now) const {
  String out;
  appendSeriesMDHM(out, now, F(" temp"), temperature_forecast);
  appendSeriesMDHM(out, now, F(" dwpt"), dew_point_forecast);
  appendSeriesMDHM(out, now, F(" wspd"), wind_speed_forecast);
  appendSeriesMDHM(out, now, F(" wgst"), wind_gust_forecast);
  appendSeriesMDHM(out, now, F(" wdir"), wind_dir_forecast);
  appendSeriesMDHM(out, now, F(" day"), daylight_forecast);
  appendSeriesMDHM(out, now, F(" clds"), cloud_cover_forecast);
  appendSeriesMDHM(out, now, F(" prcp"), precip_type_forecast);
  appendSeriesMDHM(out, now, F(" pop"), precip_prob_forecast);
  return out;
}

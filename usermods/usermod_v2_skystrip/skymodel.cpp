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
  return out;
}

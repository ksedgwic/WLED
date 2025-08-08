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

  DEBUG_PRINTF("SkyStrip: SkyModel::update: %s\n", toString(now).c_str());

  return *this;
}

String SkyModel::toString(time_t now) const {
  String out;

  // Format our local fetch timestamp
  char nowBuf[20];
  time_util::fmt_local(nowBuf, sizeof(nowBuf), now);
  out = nowBuf;

  char dpBuf[20];

  out += F(": temp:");
  out += F("[");
  for (const auto &dp : temperature_forecast) {
    time_util::fmt_local(dpBuf, sizeof(dpBuf), dp.tstamp);
    out += " (";
    out += dpBuf;
    out += ", ";
    out += String(dp.value, 2);
    out += ")";
  }
  out += F(" ]");

  out += F(": dew_point:");
  out += F("[");
  for (const auto &dp : dew_point_forecast) {
    time_util::fmt_local(dpBuf, sizeof(dpBuf), dp.tstamp);
    out += " (";
    out += dpBuf;
    out += ", ";
    out += String(dp.value, 2);
    out += ")";
  }
  out += F(" ]");

  return out;
}

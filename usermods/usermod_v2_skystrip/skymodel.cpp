#include "skymodel.h"
#include "wled.h"
#include <cassert>

SkyModel & SkyModel::update(time_t now, SkyModel && other) {
  lcl_tstamp = other.lcl_tstamp;

  if (!other.temperature_forecast.empty())
    temperature_forecast.swap(other.temperature_forecast);

  DEBUG_PRINTF("SkyStrip: SkyModel::update: %s\n", utcOffsetSecs, toString(now).c_str());

  return *this;
}

String SkyModel::toString(time_t now) const {
  String out;

  // Format our local fetch timestamp
  time_t ourTs = lcl_tstamp + utcOffsetSecs; // convert to local
  struct tm tmNow = *localtime(&ourTs);
  char nowBuf[20];
  snprintf(nowBuf, sizeof(nowBuf),
           "%02d-%02d %02d:%02d", // <-- space between date and time
           tmNow.tm_mon + 1, tmNow.tm_mday, tmNow.tm_hour,
           tmNow.tm_min);
  out = nowBuf;

  out += F(": temp:");
  out += F("[");
  char buf[20];
  for (const auto &dp : temperature_forecast) {
    time_t tstamp = dp.tstamp + utcOffsetSecs; // convert to local
    struct tm tmTstamp = *localtime(&tstamp);
    snprintf(buf, sizeof(buf),
             "%02d-%02d %02d:%02d", // <-- space between date and time
             tmTstamp.tm_mon + 1, tmTstamp.tm_mday, tmTstamp.tm_hour,
             tmTstamp.tm_min);
    out += " (";
    out += buf;
    out += ", ";
    out += String(dp.value, 2);
    out += ")";
  }
  out += F(" ]");

  return out;
}

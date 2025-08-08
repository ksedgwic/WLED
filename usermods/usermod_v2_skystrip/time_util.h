// util.h
#pragma once
#include "wled.h"
#include <ctime>
#include <cstdint>

// NOTE - this utility is a workaround because time(nullptr) and localtime_r
// don't work on WLED.

// util.h
#pragma once
#include "wled.h"
#include <ctime>
#include <cstdint>

namespace time_util {

// UTC now from WLED’s clock (same source the UI uses)
static inline time_t time_now_utc() {
  return (time_t)toki.getTime().sec;
}

// Current UTC→local offset in seconds (derived from WLED’s own localTime)
static inline long current_offset() {
  long off = (long)localTime - (long)toki.getTime().sec;
  // sanity clamp ±15h (protects against early-boot junk)
  if (off < -54000 || off > 54000) off = 0;
  return off;
}

// Format any UTC epoch using WLED’s *current* offset
static inline void fmt_local(char* out, size_t n, time_t utc_ts,
                             const char* fmt = "%m-%d %H:%M") {
  const time_t local_sec = utc_ts + current_offset();
  struct tm tmLocal;
  gmtime_r(&local_sec, &tmLocal);      // local_sec is already local seconds
  strftime(out, n, fmt, &tmLocal);
}

} // namespace time_util

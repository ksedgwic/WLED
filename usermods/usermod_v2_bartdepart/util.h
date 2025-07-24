#pragma once

#include <ctime>
#include "wled.h"  // brings in the global `localTime`

/**
 * @brief  Returns the current wall‑clock time (seconds since Unix epoch),
 *         as driven by WLED’s SNTP client.  Will be 0 until the first sync.
 */
inline time_t now() {
  return ::localTime;
}

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "wled.h"

#include "train_platform_model.h"
#include "util.h"

void TrainPlatformModel::update(const JsonObject &root) {
  if (platformId_.isEmpty()) return;

  ETDBatch batch;
  const char* dateStr = root["date"] | "";
  const char* timeStr = root["time"] | "";
  batch.apiTs = parseHeaderTimestamp(dateStr, timeStr);
  batch.ourTs = util::time_now();

  if (root["station"].is<JsonArray>()) {
    for (JsonObject station : root["station"].as<JsonArray>()) {
      if (!station["etd"].is<JsonArray>()) continue;
      for (JsonObject etd : station["etd"].as<JsonArray>()) {
        if (!etd["estimate"].is<JsonArray>()) continue;
        for (JsonObject est : etd["estimate"].as<JsonArray>()) {
          if (String(est["platform"] | "0") != platformId_) continue;
          int mins = atoi(est["minutes"] | "0");
          time_t dep = batch.apiTs + mins * 60;
          TrainColor col = parseTrainColor(est["color"] | "");
          batch.etds.push_back(ETD{dep, col});
        }
      }
    }
  }

  // sort by estDep ascending
  std::sort(batch.etds.begin(), batch.etds.end(),
    [](const ETD& a, const ETD& b) {
      return a.estDep < b.estDep;
    });

  // keep the most recent history
  history_.push_back(std::move(batch));
  while (history_.size() > 5) {
    history_.pop_front();
  }

  DEBUG_PRINTF("BartDepart::update platform %s: %s\n", platformId_.c_str(), toString().c_str());
}

// helper to map your TrainColor enum → CRGB
static CRGB colorFromTrainColor(TrainColor tc) {
  switch(tc) {
    case TrainColor::Red:    return CRGB(255,  0,  0);
    case TrainColor::Orange: return CRGB(255,150, 30);
    case TrainColor::Yellow: return CRGB(255,255,  0);
    case TrainColor::Green:  return CRGB(  0,255,  0);
    case TrainColor::Blue:   return CRGB(  0,  0,255);
    case TrainColor::White:  return CRGB(255,255,255);
    default:                 return CRGB(  0,  0,  0);
  }
}

void TrainPlatformModel::display(time_t now, size_t segment, uint16_t updateSecs) const {
  if (platformId_.isEmpty()) return;
  if (history_.empty()) return;

  // phase toggle
  static uint8_t frameCnt = 0;           // 0..99
  bool preferFirst = frameCnt < 50;      // true for first 0.5 s
  frameCnt = (frameCnt + 1) % 100;

  const ETDBatch& batch = history_.back();

  // fetch segment start/stop
  auto &seg = strip.getSegment(segment);
  seg.freeze = true;
  int  start = seg.start;
  int    end = seg.stop - 1;    // inclusive
  int    len = end - start + 1;

  // clear it
  for (int i = start; i <= end; i++) {
    strip.setPixelColor(i, 0);  // off
  }

  // Is this segment flagged as "reversed"?
  int base  = seg.reverse ? end : start;
  int dir   = seg.reverse ? -1  : +1;

  // for each ETD, plot it
  for (auto &e : batch.etds) {
    // We offset the display by updateSecs because when a train is
    // held in the station (common) it's departure keeps getting
    // delayed and we don't want it falling off the bottom of the
    // display before we get the next update (telling us it is still
    // there).
    float diffMin = float(updateSecs + e.estDep - now) / 60.0f;
    if (diffMin < 0 || diffMin >= len) continue;

    int   idx  = int(floor(diffMin));        // primary LED index
    float frac = diffMin - float(idx);       // cross‐fade fraction
    uint8_t b1 = uint8_t((1.0f - frac) * 255);
    uint8_t b2 = uint8_t(frac * 255);

    CRGB col = colorFromTrainColor(e.color);

    int pos1 = base + dir*idx;
    int pos2 = base + dir*(idx+1);

    // When trains "overlap" (because they are sourced from two different lines) we:
    // 1. Generally alternate, prefering the first a while and then the other.
    // 2. Except when one is much brighter than the other, in which case prefer it.
    //    This keeps a "sliver" of an adjacent train from interfering.

    // compute “brightness” = R+G+B
    auto brightness = [&](uint32_t c) {
      return (uint16_t)(((c >> 16) & 0xFF) + ((c >> 8) & 0xFF) + (c & 0xFF));
    };

    // primary LED gets (1–frac)×full brightness
    if (pos1 >= start && pos1 <= end) {
      uint32_t existing = strip.getPixelColor(pos1);
      // build new color
      uint32_t newColor1 =
        (((uint32_t)col.r * b1 / 255) << 16) |
        (((uint32_t)col.g * b1 / 255) <<  8) |
         ((uint32_t)col.b * b1 / 255);

      uint16_t oldB = brightness(existing);
      uint16_t newB = brightness(newColor1);

      if ((preferFirst && (existing == 0 || newB > 2*oldB))
          || (!preferFirst && newB * 2 >= oldB)) {
        strip.setPixelColor(pos1, newColor1);
      }
    }

    // secondary LED (if in bounds) gets frac×full brightness
    if (pos2 >= start && pos2 <= end) {
      uint32_t existing2 = strip.getPixelColor(pos2);
      uint32_t newColor2 =
        (((uint32_t)col.r * b2 / 255) << 16) |
        (((uint32_t)col.g * b2 / 255) <<  8) |
         ((uint32_t)col.b * b2 / 255);

      uint16_t oldB2 = brightness(existing2);
      uint16_t newB2 = brightness(newColor2);
      if ((preferFirst && (existing2 == 0 || newB2 > 2*oldB2))
          || (!preferFirst && newB2 * 2 >= oldB2)) {
        strip.setPixelColor(pos2, newColor2);
      }
    }
  }
}

void TrainPlatformModel::merge(const TrainPlatformModel& other) {
  for (auto const& b : other.history_) {
    history_.push_back(b);
    if (history_.size() > 5) history_.pop_front();
  }
}

time_t TrainPlatformModel::oldest() const {
  if (history_.empty()) return 0;
  return history_.front().ourTs;
}

time_t TrainPlatformModel::parseHeaderTimestamp(const char* dateStr, const char* timeStr) const {
  // dateStr is "MM/DD/YYYY", timeStr is "HH:MM:SS AM/PM"
  int month=0, day=0, year=0;
  int hour=0, min=0, sec=0;
  char ampm[3] = {0};

  // parse date
  sscanf(dateStr, "%d/%d/%d", &month, &day, &year);
  // parse time and AM/PM
  sscanf(timeStr, "%d:%d:%d %2s", &hour, &min, &sec, ampm);

  // adjust hour for AM/PM
  if (strcasecmp(ampm, "PM") == 0 && hour < 12) hour += 12;
  if (strcasecmp(ampm, "AM") == 0 && hour == 12) hour = 0;

  struct tm tm{};
  tm.tm_year = year - 1900;
  tm.tm_mon  = month - 1;
  tm.tm_mday = day;
  tm.tm_hour = hour;
  tm.tm_min  = min;
  tm.tm_sec  = sec;

  // mktime assumes tm is local time
  return mktime(&tm);
}

String TrainPlatformModel::toString() const {
  // Summarize our state in  a string that looks like:
  // 18:04:48: lag 16: +8 (18:13:32:ORANGE) +8 (18:21:32:RED) +12 (18:33:32:ORANGE)

  if (history_.empty()) return String();

  // Grab the most recent batch
  const ETDBatch& batch = history_.back();
  const auto& etds = batch.etds;
  if (etds.empty()) return String();

  // Format our local fetch timestamp
  time_t ourTs = batch.ourTs;
  struct tm tmNow = *localtime(&ourTs);
  char nowBuf[16];
  snprintf(nowBuf, sizeof(nowBuf),
           "%02d:%02d:%02d",
           tmNow.tm_hour,
           tmNow.tm_min,
           tmNow.tm_sec);

  // Compute lag in seconds between ourTs and apiTs
  int lagSecs = ourTs - batch.apiTs;

  // Build the string, start with local time and lag
  String out;
  out = nowBuf;
  out += F(": lag ");
  if (lagSecs < 10)
    out += ' ';
  out += String(lagSecs);
  out += F(":");

  // For each ETD, compute the minute‐delta from ourTs (then successive)
  time_t prevTs = ourTs;
  char buf[16];
  for (const auto& e : etds) {
    time_t depTs = e.estDep;
    int deltaMin = int((depTs - prevTs) / 60);
    prevTs = depTs;

    struct tm tmDep = *localtime(&depTs);
    snprintf(buf, sizeof(buf),
             "%02d:%02d:%02d",
             tmDep.tm_hour,
             tmDep.tm_min,
             tmDep.tm_sec);

    out += F(" +");
    out += String(deltaMin);
    out += F(" (");
    out += buf;
    out += F(":");
    out += ::toString(e.color);
    out += F(")");
  }

  return out;
}

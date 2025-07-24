#include <algorithm>
#include <cstdio>
#include <cstring>

#include "train_platform_model.h"
#include "util.h"

void TrainPlatformModel::update(const JsonObject &root) {
  ETDBatch batch;
  const char* dateStr = root["date"] | "";
  const char* timeStr = root["time"] | "";
  batch.apiTs = parseHeaderTimestamp(dateStr, timeStr);
  batch.ourTs = now();

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

  history_.push_back(std::move(batch));
  while (history_.size() > 5) {
    history_.pop_front();
  }
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

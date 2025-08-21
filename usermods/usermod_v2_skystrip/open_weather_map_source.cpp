#include <limits>
#include <cstdlib>
#include <cmath>

#include "open_weather_map_source.h"
#include "skymodel.h"
#include "util.h"

static constexpr const char* DEFAULT_API_BASE    =
  "https://api.openweathermap.org/data/3.0/onecall"
  "?exclude=minutely,daily,alerts"
  "&units=imperial";
static constexpr const char * DEFAULT_API_KEY = "";
static constexpr const char * DEFAULT_LOCATION = "";
static constexpr const double DEFAULT_LATITUDE = 37.80486;
static constexpr const double DEFAULT_LONGITUDE = -122.2716;
static constexpr unsigned DEFAULT_INTERVAL_SEC = 3600;	// 1 hour

// - these are user visible in the webapp settings UI
// - they are scoped to this module, don't need to be globally unique
//
const char CFG_API_BASE[] = "ApiBase";
const char CFG_API_KEY[] = "ApiKey";
const char CFG_LATITUDE[] = "Latitude";
const char CFG_LONGITUDE[] = "Longitude";
const char CFG_INTERVAL_SEC[] = "IntervalSec";
const char CFG_LOCATION[] = "Location";

// keep commas; encode spaces etc.
static String urlEncode(const String& s) {
  static const char hex[] = "0123456789ABCDEF";
  String out; out.reserve(s.length()*3);
  for (size_t i = 0; i < s.length(); ++i) {
    uint8_t c = (uint8_t)s[i];
    if ((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||
        c=='-'||c=='_'||c=='.'||c=='~'||c==',')
      out += (char)c;
    else if (c==' ') out += "%20";
    else { out += '%'; out += hex[c>>4]; out += hex[c&0xF]; }
  }
  return out;
}

// Normalize "Oakland, CA, USA" → "Oakland,CA,US"
static String normalizeLocation(String q) {
  q.trim();
  q.replace(" ", "");
  q.replace(",USA", ",US");
  return q;
}

static bool parseCoordToken(String token, double& out) {
  token.trim();
  token.toLowerCase();
  bool neg = false;
  if (token.startsWith("s") || token.startsWith("w")) {
    neg = true;
    token.remove(0, 1);
  } else if (token.startsWith("n") || token.startsWith("e")) {
    token.remove(0, 1);
  }
  token.trim();
  if (token.endsWith("s") || token.endsWith("w")) {
    neg = true;
    token.remove(token.length() - 1);
  } else if (token.endsWith("n") || token.endsWith("e")) {
    token.remove(token.length() - 1);
  }
  token.trim();
  token.replace("°", " ");
  token.replace("\"", " ");
  token.replace("'", " ");
  token.trim();
  char* end = nullptr;
  double deg = strtod(token.c_str(), &end);
  if (end == token.c_str()) return false;
  bool negNum = deg < 0;
  deg = fabs(deg);
  String rest(end);
  rest.trim();
  double min = 0, sec = 0;
  if (rest.length() > 0) {
    char* end2 = nullptr;
    min = strtod(rest.c_str(), &end2);
    if (end2 != rest.c_str()) {
      String rest2(end2);
      rest2.trim();
      if (rest2.length() > 0) {
        sec = strtod(rest2.c_str(), &end2);
      }
    }
  }
  if (negNum) neg = true;
  out = deg + min / 60.0 + sec / 3600.0;
  if (neg) out = -out;
  return true;
}

static bool parseLatLon(String s, double& lat, double& lon) {
  s.trim();
  int comma = s.indexOf(',');
  String a, b;
  if (comma >= 0) {
    a = s.substring(0, comma);
    b = s.substring(comma + 1);
  } else {
    int space = s.lastIndexOf(' ');
    if (space < 0) return false;
    a = s.substring(0, space);
    b = s.substring(space + 1);
  }
  a.trim();
  b.trim();
  if (a.length() == 0 || b.length() == 0) return false;
  if (!parseCoordToken(a, lat)) return false;
  if (!parseCoordToken(b, lon)) return false;
  return true;
}

OpenWeatherMapSource::OpenWeatherMapSource()
    : apiBase_(DEFAULT_API_BASE)
    , apiKey_(DEFAULT_API_KEY)
    , location_(DEFAULT_LOCATION)
    , latitude_(DEFAULT_LATITUDE)
    , longitude_(DEFAULT_LONGITUDE)
    , intervalSec_(DEFAULT_INTERVAL_SEC)
    , lastFetch_(0)
    , lastHistFetch_(0) {
  DEBUG_PRINTF("SkyStrip: %s::CTOR\n", name().c_str());

}

void OpenWeatherMapSource::addToConfig(JsonObject& subtree) {
  subtree[FPSTR(CFG_API_BASE)] = apiBase_;
  subtree[FPSTR(CFG_API_KEY)] = apiKey_;
  subtree[FPSTR(CFG_LOCATION)] = location_;
  subtree[FPSTR(CFG_LATITUDE)] = latitude_;
  subtree[FPSTR(CFG_LONGITUDE)] = longitude_;
  subtree[FPSTR(CFG_INTERVAL_SEC)] = intervalSec_;
}

bool OpenWeatherMapSource::readFromConfig(JsonObject &subtree,
                                          bool running,
                                          bool& invalidate_history) {
  // note what the prior values of latitude_ and longitude_ are
  double oldLatitude = latitude_;
  double oldLongitude = longitude_;

  bool configComplete = !subtree.isNull();
  configComplete &= getJsonValue(subtree[FPSTR(CFG_API_BASE)], apiBase_, DEFAULT_API_BASE);
  configComplete &= getJsonValue(subtree[FPSTR(CFG_API_KEY)], apiKey_, DEFAULT_API_KEY);
  configComplete &= getJsonValue(subtree[FPSTR(CFG_LOCATION)], location_, DEFAULT_LOCATION);
  configComplete &= getJsonValue(subtree[FPSTR(CFG_LATITUDE)], latitude_, DEFAULT_LATITUDE);
  configComplete &= getJsonValue(subtree[FPSTR(CFG_LONGITUDE)], longitude_, DEFAULT_LONGITUDE);
  configComplete &= getJsonValue(subtree[FPSTR(CFG_INTERVAL_SEC)], intervalSec_, DEFAULT_INTERVAL_SEC);

  // If the location changed update lat/long via parsing or lookup
  if (location_ == lastLocation_) {
    // if the user changed the lat and long directly clear the location
    if (latitude_ != oldLatitude || longitude_ != oldLongitude)
      location_ = "";
  } else {
    lastLocation_ = location_;
    if (location_.length() > 0) {
      double lat = 0, lon = 0;
      if (parseLatLon(String(location_.c_str()), lat, lon)) {
        latitude_ = lat;
        longitude_ = lon;
      } else if (running) {
        int matches = 0;
        bool ok = geocodeOWM(location_, lat, lon, &matches);
        latitude_ = ok ? lat : 0.0;
        longitude_ = ok ? lon : 0.0;
      }
    }
  }

  // if the lat/long changed we need to invalidate_history
  if (latitude_ != oldLatitude || longitude_ != oldLongitude)
    invalidate_history = true;

  return configComplete;
}

String OpenWeatherMapSource::composeApiUrl() {
  char buf[1024];
  (void)snprintf(buf, sizeof(buf), "%s&lat=%.6f&lon=%.6f&appid=%s",
                 apiBase_.c_str(), latitude_, longitude_, apiKey_.c_str());
  // optional: guard against overflow
  buf[sizeof(buf) - 1] = '\0';
  return String(buf);
}

std::unique_ptr<SkyModel> OpenWeatherMapSource::fetch(std::time_t now) {
  // Wait for scheduled time
  if ((now - lastFetch_) < static_cast<std::time_t>(intervalSec_))
    return nullptr;

  lastFetch_ = now;

  // Fetch JSON
  String url = composeApiUrl();
  DEBUG_PRINTF("SkyStrip: %s::fetch URL: %s\n", name().c_str(), url.c_str());

  auto doc = getJson(url);
  if (!doc) {
    DEBUG_PRINTF("SkyStrip: %s::fetch failed: no JSON\n", name().c_str());
    return nullptr;
  }

  // Top-level object
  JsonObject root = doc->as<JsonObject>();

  if (!root.containsKey("hourly")) {
    DEBUG_PRINTF("SkyStrip: %s::fetch failed: no \"hourly\" field\n", name().c_str());
    return nullptr;
  }

  time_t sunrise = 0;
  time_t sunset = 0;
  if (root.containsKey("current")) {
    JsonObject cur = root["current"].as<JsonObject>();
    if (cur.containsKey("sunrise") && cur.containsKey("sunset")) {
      sunrise = cur["sunrise"].as<time_t>();
      sunset  = cur["sunset"].as<time_t>();
    } else {
      bool night = false;
      JsonArray wArrCur = cur["weather"].as<JsonArray>();
      if (!wArrCur.isNull() && wArrCur.size() > 0) {
        String icon = wArrCur[0]["icon"] | String("");
        if (icon.endsWith("n")) night = true;
      }
      if (night) {
        sunrise = std::numeric_limits<time_t>::max();
        sunset = 0;
      } else {
        sunrise = 0;
        sunset = std::numeric_limits<time_t>::max();
      }
    }
  }

  // Iterate the hourly array
  JsonArray hourly = root["hourly"].as<JsonArray>();
  auto model = ::make_unique<SkyModel>();
  model->lcl_tstamp = now;
  model->sunrise_ = sunrise;
  model->sunset_ = sunset;
  for (JsonObject hour : hourly) {
    time_t dt    = hour["dt"].as<time_t>();
    model->temperature_forecast.push_back({ dt, hour["temp"].as<double>() });
    model->dew_point_forecast.push_back({ dt, hour["dew_point"].as<double>() });
    model->wind_speed_forecast.push_back({ dt, hour["wind_speed"].as<double>() });
    model->wind_dir_forecast.push_back({ dt, hour["wind_deg"].as<double>() });
    model->wind_gust_forecast.push_back({ dt, hour["wind_gust"].as<double>() });
    model->cloud_cover_forecast.push_back({ dt, hour["clouds"].as<double>() });
    JsonArray wArr = hour["weather"].as<JsonArray>();
    bool hasRain = false, hasSnow = false;
    if (hour.containsKey("rain")) {
      double v = hour["rain"]["1h"] | 0.0;
      if (v > 0.0) hasRain = true;
    }
    if (hour.containsKey("snow")) {
      double v = hour["snow"]["1h"] | 0.0;
      if (v > 0.0) hasSnow = true;
    }
    if (!hasRain && !hasSnow && !wArr.isNull() && wArr.size() > 0) {
      String main = wArr[0]["main"] | String("");
      main.toLowerCase();
      if (main == F("rain") || main == F("drizzle") || main == F("thunderstorm"))
        hasRain = true;
      else if (main == F("snow"))
        hasSnow = true;
    }
    int ptype = hasRain && hasSnow ? 3 : (hasSnow ? 2 : (hasRain ? 1 : 0));
    model->precip_type_forecast.push_back({ dt, double(ptype) });
    model->precip_prob_forecast.push_back({ dt, hour["pop"].as<double>() });
  }

  return model;
}

std::unique_ptr<SkyModel> OpenWeatherMapSource::checkhistory(time_t now, std::time_t oldestTstamp) {
  if (oldestTstamp == 0) return nullptr;
  if ((now - lastHistFetch_) < 15) return nullptr;
  lastHistFetch_ = now;

  static constexpr time_t HISTORY_SEC = 24 * 60 * 60;
  if (oldestTstamp <= now - HISTORY_SEC) return nullptr;

  time_t fetchDt = oldestTstamp - 3600;
  char buf[256];
  snprintf(buf, sizeof(buf),
           "https://api.openweathermap.org/data/3.0/onecall/timemachine?lat=%.6f&lon=%.6f&dt=%ld&units=imperial&appid=%s",
           latitude_, longitude_, (long)fetchDt, apiKey_.c_str());
  String url(buf);
  DEBUG_PRINTF("SkyStrip: %s::checkhistory URL: %s\n", name().c_str(), url.c_str());

  auto doc = getJson(url);
  if (!doc) {
    DEBUG_PRINTF("SkyStrip: %s::checkhistory failed: no JSON\n", name().c_str());
    return nullptr;
  }

  JsonObject root = doc->as<JsonObject>();
  JsonArray hourly = root["hourly"].as<JsonArray>();
  if (hourly.isNull()) hourly = root["data"].as<JsonArray>();
  if (hourly.isNull()) {
    DEBUG_PRINTF("SkyStrip: %s::checkhistory failed: no hourly/data field\n", name().c_str());
    return nullptr;
  }

  auto model = ::make_unique<SkyModel>();
  model->lcl_tstamp = now;
  model->sunrise_ = 0;
  model->sunset_ = 0;
  for (JsonObject hour : hourly) {
    time_t dt = hour["dt"].as<time_t>();
    if (dt >= oldestTstamp) continue;
    model->temperature_forecast.push_back({ dt, hour["temp"].as<double>() });
    model->dew_point_forecast.push_back({ dt, hour["dew_point"].as<double>() });
    model->wind_speed_forecast.push_back({ dt, hour["wind_speed"].as<double>() });
    model->wind_dir_forecast.push_back({ dt, hour["wind_deg"].as<double>() });
    model->wind_gust_forecast.push_back({ dt, hour["wind_gust"].as<double>() });
    model->cloud_cover_forecast.push_back({ dt, hour["clouds"].as<double>() });
    JsonArray wArr = hour["weather"].as<JsonArray>();
    bool hasRain = false, hasSnow = false;
    if (hour.containsKey("rain")) {
      double v = hour["rain"]["1h"] | 0.0;
      if (v > 0.0) hasRain = true;
    }
    if (hour.containsKey("snow")) {
      double v = hour["snow"]["1h"] | 0.0;
      if (v > 0.0) hasSnow = true;
    }
    if (!hasRain && !hasSnow && !wArr.isNull() && wArr.size() > 0) {
      String main = wArr[0]["main"] | String("");
      main.toLowerCase();
      if (main == F("rain") || main == F("drizzle") || main == F("thunderstorm"))
        hasRain = true;
      else if (main == F("snow"))
        hasSnow = true;
    }
    int ptype = hasRain && hasSnow ? 3 : (hasSnow ? 2 : (hasRain ? 1 : 0));
    model->precip_type_forecast.push_back({ dt, double(ptype) });
    model->precip_prob_forecast.push_back({ dt, hour["pop"].as<double>() });
  }

  if (model->temperature_forecast.empty()) return nullptr;
  return model;
}

void OpenWeatherMapSource::reload(std::time_t now) {
  const std::time_t iv = static_cast<std::time_t>(intervalSec_);
  // Force next fetch to be eligible immediately
  lastFetch_ = (now >= iv) ? (now - iv) : 0;

  // If you later add backoff/jitter, clear it here too.
  // backoffExp_ = 0; nextRetryAt_ = 0;
  DEBUG_PRINTF("SkyStrip: %s::reload (interval=%u)\n", name().c_str(), intervalSec_);
}

// Returns true iff exactly one match; sets lat/lon. Otherwise zeros them.
bool OpenWeatherMapSource::geocodeOWM(std::string const & rawQuery,
                                      double& lat, double& lon,
                                      int* outMatches)
{
  lat = lon = 0;
  String q = normalizeLocation(String(rawQuery.c_str()));
  if (q.isEmpty()) { if (outMatches) *outMatches = 0; return false; }

  String url = F("https://api.openweathermap.org/geo/1.0/direct?q=");
  url += urlEncode(q);
  url += F("&limit=5&appid=");
  url += apiKey_.c_str();

  auto doc = getJson(url);
  resetRateLimit();	// we want to do a fetch immediately after ...
  if (!doc || !doc->is<JsonArray>()) {
    if (outMatches) *outMatches = -1;
    DEBUG_PRINTF("SkyStrip: %s::geocodeOWM failed\n", name().c_str());
    return false;
  }

  JsonArray arr = doc->as<JsonArray>();
  DEBUG_PRINTF("SkyStrip: %s::geocodeOWM %d matches found\n", name().c_str(), arr.size());
  if (outMatches) *outMatches = arr.size();
  if (arr.size() == 1) {
    lat = arr[0]["lat"] | 0.0;
    lon = arr[0]["lon"] | 0.0;
    return true;
  }
  return false;
}

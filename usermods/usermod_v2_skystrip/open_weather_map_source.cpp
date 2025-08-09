#include "open_weather_map_source.h"
#include "skymodel.h"

static constexpr const char* DEFAULT_API_BASE    =
  "https://api.openweathermap.org/data/3.0/onecall"
  "?exclude=current,minutely,daily,alerts"
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

OpenWeatherMapSource::OpenWeatherMapSource()
    : apiBase_(DEFAULT_API_BASE)
    , apiKey_(DEFAULT_API_KEY)
    , location_(DEFAULT_LOCATION)
    , latitude_(DEFAULT_LATITUDE)
    , longitude_(DEFAULT_LONGITUDE)
    , intervalSec_(DEFAULT_INTERVAL_SEC)
    , lastFetch_(0) {
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

bool OpenWeatherMapSource::readFromConfig(JsonObject &subtree, bool running) {
  bool configComplete = !subtree.isNull();
  configComplete &= getJsonValue(subtree[FPSTR(CFG_API_BASE)], apiBase_, DEFAULT_API_BASE);
  configComplete &= getJsonValue(subtree[FPSTR(CFG_API_KEY)], apiKey_, DEFAULT_API_KEY);
  configComplete &= getJsonValue(subtree[FPSTR(CFG_LOCATION)], location_, DEFAULT_LOCATION);
  configComplete &= getJsonValue(subtree[FPSTR(CFG_LATITUDE)], latitude_, DEFAULT_LATITUDE);
  configComplete &= getJsonValue(subtree[FPSTR(CFG_LONGITUDE)], longitude_, DEFAULT_LONGITUDE);
  configComplete &= getJsonValue(subtree[FPSTR(CFG_INTERVAL_SEC)], intervalSec_, DEFAULT_INTERVAL_SEC);

  // If we are safely past the boot/startup phase we can make API
  // calls to update lat/long ...
  if (running) {
    if (location_ != lastLocation_) {
      lastLocation_ = location_;
      if (location_.length() > 0) {
        double lat=0, lon=0; int matches=0;
        bool ok = geocodeOWM(location_, lat, lon, &matches);
        latitude_ = ok ? lat : 0.0;
        longitude_ = ok ? lon : 0.0;
      }
    }
  }
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
  // Iterate the hourly array
  JsonArray hourly = root["hourly"].as<JsonArray>();
  auto model = ::make_unique<SkyModel>();
  model->lcl_tstamp = now;
  for (JsonObject hour : hourly) {
    time_t dt    = hour["dt"].as<time_t>();
    model->temperature_forecast.push_back({ dt, hour["temp"].as<double>() });
    model->dew_point_forecast.push_back({ dt, hour["dew_point"].as<double>() });
    model->wind_speed_forecast.push_back({ dt, hour["wind_speed"].as<double>() });
    model->wind_dir_forecast.push_back({ dt, hour["wind_deg"].as<double>() });
    model->wind_gust_forecast.push_back({ dt, hour["wind_gust"].as<double>() });
  }

  return model;
}

void OpenWeatherMapSource::reset(std::time_t now) {
  const std::time_t iv = static_cast<std::time_t>(intervalSec_);
  // Force next fetch to be eligible immediately
  lastFetch_ = (now >= iv) ? (now - iv) : 0;

  // If you later add backoff/jitter, clear it here too.
  // backoffExp_ = 0; nextRetryAt_ = 0;
  DEBUG_PRINTF("SkyStrip: %s::reset (interval=%u)\n", name().c_str(), intervalSec_);
}

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

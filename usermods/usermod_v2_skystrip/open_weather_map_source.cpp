#include "open_weather_map_source.h"
#include "skymodel.h"

static constexpr const char* DEFAULT_API_BASE    =
  "https://api.openweathermap.org/data/3.0/onecall"
  "?exclude=current,minutely,daily,alerts"
  "&units=imperial";
static constexpr const char *DEFAULT_API_KEY = "";
static constexpr const double DEFAULT_LATITUDE = 37.8044;
static constexpr const double DEFAULT_LONGITUDE = -122.2712;
static constexpr unsigned DEFAULT_INTERVAL_SEC = 3600;	// 1 hour

// - these are user visible in the webapp settings UI
// - they are scoped to this module, don't need to be globally unique
//
const char CFG_API_BASE[] = "ApiBase";
const char CFG_API_KEY[] = "ApiKey";
const char CFG_LATITUDE[] = "Latitude";
const char CFG_LONGITUDE[] = "Longitude";
const char CFG_INTERVAL_SEC[] = "IntervalSec";

OpenWeatherMapSource::OpenWeatherMapSource()
    : apiBase_(DEFAULT_API_BASE)
    , apiKey_(DEFAULT_API_KEY)
    , latitude_(DEFAULT_LATITUDE)
    , longitude_(DEFAULT_LONGITUDE)
    , intervalSec_(DEFAULT_INTERVAL_SEC)
    , lastFetch_(0) {
  DEBUG_PRINTF("SkyStrip: %s::CTOR\n", name().c_str());

}

void OpenWeatherMapSource::addToConfig(JsonObject& subtree) {
  subtree[FPSTR(CFG_API_BASE)] = apiBase_;
  subtree[FPSTR(CFG_API_KEY)] = apiKey_;
  subtree[FPSTR(CFG_LATITUDE)] = latitude_;
  subtree[FPSTR(CFG_LONGITUDE)] = longitude_;
  subtree[FPSTR(CFG_INTERVAL_SEC)] = intervalSec_;
}

bool OpenWeatherMapSource::readFromConfig(JsonObject &subtree) {
  bool configComplete = !subtree.isNull();
  configComplete &= getJsonValue(subtree[FPSTR(CFG_API_BASE)], apiBase_, DEFAULT_API_BASE);
  configComplete &= getJsonValue(subtree[FPSTR(CFG_API_KEY)], apiKey_, DEFAULT_API_KEY);
  configComplete &= getJsonValue(subtree[FPSTR(CFG_LATITUDE)], latitude_, DEFAULT_LATITUDE);
  configComplete &= getJsonValue(subtree[FPSTR(CFG_LONGITUDE)], longitude_, DEFAULT_LONGITUDE);
  configComplete &= getJsonValue(subtree[FPSTR(CFG_INTERVAL_SEC)], intervalSec_, DEFAULT_INTERVAL_SEC);
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
    double tempK = hour["temp"].as<double>();
    model->temperature_forecast.push_back({ dt, tempK });
  }

  return model;
}

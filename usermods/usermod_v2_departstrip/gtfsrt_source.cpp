#include "gtfsrt_source.h"

GtfsRtSource::GtfsRtSource(const char* key) {
  if (key && *key) configKey_ = key;
}

std::unique_ptr<DepartModel> GtfsRtSource::fetch(std::time_t now) {
  if (!enabled_ || now == 0) return nullptr;
  if (now < nextFetch_) return nullptr;
  nextFetch_ = now + updateSecs_;
  DEBUG_PRINTF("DepartStrip: GtfsRtSource::fetch: placeholder poll for %s\n", sourceKey().c_str());
  return nullptr;
}

void GtfsRtSource::reload(std::time_t now) {
  nextFetch_ = now;
}

String GtfsRtSource::sourceKey() const {
  String k(agency_);
  k += ':';
  k += stopCode_;
  return k;
}

void GtfsRtSource::addToConfig(JsonObject& root) {
  root["Enabled"] = enabled_;
  root["Type"] = F("gtfsrt");
  root["UpdateSecs"] = updateSecs_;
  root["TemplateUrl"] = baseUrl_;
  root["ApiKey"] = apiKey_;
  String key;
  key.reserve(agency_.length() + 1 + stopCode_.length());
  key = agency_;
  key += ':';
  key += stopCode_;
  root["AgencyStopCode"] = key;
}

bool GtfsRtSource::readFromConfig(JsonObject& root, bool startup_complete, bool& invalidate_history) {
  bool ok = true;
  bool prevEnabled = enabled_;
  String prevAgency = agency_;
  String prevStop = stopCode_;
  String prevBase = baseUrl_;

  ok &= getJsonValue(root["Enabled"], enabled_, enabled_);
  ok &= getJsonValue(root["UpdateSecs"], updateSecs_, updateSecs_);
  ok &= getJsonValue(root["TemplateUrl"], baseUrl_, baseUrl_);
  ok &= getJsonValue(root["ApiKey"], apiKey_, apiKey_);

  String keyStr;
  bool haveKey = getJsonValue(root["AgencyStopCode"], keyStr, (const char*)nullptr);
  if (!haveKey) haveKey = getJsonValue(root["Key"], keyStr, (const char*)nullptr);
  if (haveKey && keyStr.length() > 0) {
    int colon = keyStr.indexOf(':');
    if (colon > 0) {
      agency_ = keyStr.substring(0, colon);
      stopCode_ = keyStr.substring(colon + 1);
    }
  } else {
    ok &= getJsonValue(root["Agency"], agency_, agency_);
    ok &= getJsonValue(root["StopCode"], stopCode_, stopCode_);
  }

  if (updateSecs_ < 10) updateSecs_ = 10;

  invalidate_history |= (agency_ != prevAgency) || (stopCode_ != prevStop) || (baseUrl_ != prevBase);

  {
    String label = F("GtfsRtSource_");
    label += agency_;
    label += '_';
    label += stopCode_;
    configKey_ = std::string(label.c_str());
  }

  if (startup_complete && !prevEnabled && enabled_) reload(departstrip::util::time_now_utc());
  return ok;
}

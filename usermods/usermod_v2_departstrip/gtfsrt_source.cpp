#include "gtfsrt_source.h"

GtfsRtSource::GtfsRtSource(const char* key) {
  if (key && *key) configKey_ = key;
}

std::unique_ptr<DepartModel> GtfsRtSource::fetch(std::time_t now) {
  if (!enabled_ || now == 0) return nullptr;
  if (now < nextFetch_) {
    uint32_t interval = updateSecs_ > 0 ? updateSecs_ : 60;
    if (lastBackoffLog_ == 0 || now - lastBackoffLog_ >= interval) {
      lastBackoffLog_ = now;
      long rem = (long)(nextFetch_ - now);
      if (rem < 0) rem = 0;
      if (backoffMult_ > 1) {
        DEBUG_PRINTF("DepartStrip: GtfsRtSource::fetch: backoff x%u %s, next in %lds\n",
                     (unsigned)backoffMult_, sourceKey().c_str(), rem);
      } else {
        DEBUG_PRINTF("DepartStrip: GtfsRtSource::fetch: waiting %s, next in %lds\n",
                     sourceKey().c_str(), rem);
      }
    }
    return nullptr;
  }

  String url = composeUrl(agency_, stopCode_);
  String redacted = url;
  auto redactParam = [&](const __FlashStringHelper* key) {
    String k(key);
    int idx = redacted.indexOf(k);
    if (idx >= 0) {
      int valStart = idx + k.length();
      int valEnd = redacted.indexOf('&', valStart);
      if (valEnd < 0) valEnd = redacted.length();
      redacted = redacted.substring(0, valStart) + F("REDACTED") + redacted.substring(valEnd);
    }
  };
  redactParam(F("api_key="));
  redactParam(F("apikey="));
  redactParam(F("key="));
  DEBUG_PRINTF("DepartStrip: GtfsRtSource::fetch: URL: %s\n", redacted.c_str());

  int contentLen = 0;
  int httpStatus = 0;
  if (!httpBegin(url, contentLen, httpStatus)) {
    long delay = (long)updateSecs_ * (long)backoffMult_;
    DEBUG_PRINTF("DepartStrip: GtfsRtSource::fetch: scheduling backoff x%u %s for %lds (HTTP error)\n",
                 (unsigned)backoffMult_, sourceKey().c_str(), delay);
    nextFetch_ = now + delay;
    if (backoffMult_ < 16) backoffMult_ *= 2;
    return nullptr;
  }

  String ctype = http_.header("Content-Type");
  String clen = http_.header("Content-Length");
  String cenc = http_.header("Content-Encoding");
  String tenc = http_.header("Transfer-Encoding");

  DEBUG_PRINTF("DepartStrip: GtfsRtSource::fetch: status=%d type='%s' lenHint=%d contentLengthHdr=%s encoding='%s' transfer='%s'\n",
               httpStatus,
               ctype.c_str(),
               contentLen,
               clen.c_str(),
               cenc.c_str(),
               tenc.c_str());

  http_.end();

  nextFetch_ = now + updateSecs_;
  backoffMult_ = 1;
  lastBackoffLog_ = 0;
  return nullptr;
}

void GtfsRtSource::reload(std::time_t now) {
  nextFetch_ = now;
  backoffMult_ = 1;
  lastBackoffLog_ = 0;
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

String GtfsRtSource::composeUrl(const String& agency, const String& stopCode) const {
  String url = baseUrl_;
  url.replace(F("{agency}"), agency);
  url.replace(F("{AGENCY}"), agency);
  url.replace(F("{stopcode}"), stopCode);
  url.replace(F("{stopCode}"), stopCode);
  url.replace(F("{STOPCODE}"), stopCode);
  if (apiKey_.length() > 0) {
    url.replace(F("{apikey}"), apiKey_);
    url.replace(F("{apiKey}"), apiKey_);
    url.replace(F("{APIKEY}"), apiKey_);
  }
  return url;
}

bool GtfsRtSource::httpBegin(const String& url, int& outLen, int& outStatus) {
  http_.setTimeout(10000);
  if (!http_.begin(client_, url)) {
    http_.end();
    DEBUG_PRINTLN(F("DepartStrip: GtfsRtSource::fetch: begin() failed"));
    return false;
  }
  http_.useHTTP10(true);
  http_.setUserAgent("WLED-GTFSRT/0.1");
  http_.setReuse(false);
  http_.addHeader("Connection", "close");
  http_.addHeader("Accept", "application/octet-stream", true, true);
  static const char* hdrs[] = {"Content-Type", "Content-Length", "Content-Encoding", "Transfer-Encoding"};
  http_.collectHeaders(hdrs, 4);

  int status = http_.GET();
  if (status < 200 || status >= 300) {
    if (status < 0) {
      String err = HTTPClient::errorToString(status);
      DEBUG_PRINTF("DepartStrip: GtfsRtSource::fetch: HTTP error %d (%s)\n", status, err.c_str());
    } else {
      DEBUG_PRINTF("DepartStrip: GtfsRtSource::fetch: HTTP status %d\n", status);
    }
    http_.end();
    return false;
  }

  outStatus = status;
  outLen = http_.getSize();
  return true;
}

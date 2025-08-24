#include "wled.h"

#include "rest_json_client.h"

RestJsonClient::RestJsonClient()
  : lastFetchMs_(static_cast<unsigned long>(-static_cast<long>(RATE_LIMIT_MS)))
  , doc_(MAX_JSON_SIZE) {
  client_.setInsecure();
}

void RestJsonClient::resetRateLimit() {
  // pretend we just made the last fetch RATE_LIMIT_MS ago
  lastFetchMs_ = millis() - static_cast<unsigned long>(-static_cast<long>(RATE_LIMIT_MS));
}

DynamicJsonDocument* RestJsonClient::getJson(const char* url) {
  // enforce a basic rate limit to prevent runaway software from making bursts
  // of API calls (looks like DoS and get's our API key turned off ...)
  unsigned long now_ms = millis();
  if (now_ms - lastFetchMs_ < RATE_LIMIT_MS) {
    DEBUG_PRINTLN("SkyStrip: RestJsonClient::getJson: RATE LIMITED");
    return nullptr;
  }
  lastFetchMs_ = now_ms;

  HTTPClient https;
  // Begin request
  if (!https.begin(client_, url)) {
    https.end();
    DEBUG_PRINTLN(F("SkyStrip: RestJsonClient::getJson: trouble initiating request"));
    return nullptr;
  }
  int code = https.GET();
  if (code <= 0) {
    https.end();
    DEBUG_PRINTF("SkyStrip: RestJsonClient::getJson: https get error code: %d\n", code);
    return nullptr;
  }

  int len = https.getSize();
  DEBUG_PRINTF("SkyStrip: RestJsonClient::getJson: expecting up to %d bytes, free heap before deserialization: %u\n", len, ESP.getFreeHeap());
  doc_.clear();
  auto err = deserializeJson(doc_, https.getStream());
  https.end();
  if (err) {
    DEBUG_PRINTF("SkyStrip: RestJsonClient::getJson: deserialization error: %s; free heap: %u\n", err.c_str(), ESP.getFreeHeap());
    return nullptr;
  }
  return &doc_;
}

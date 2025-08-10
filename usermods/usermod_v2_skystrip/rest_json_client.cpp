#include "wled.h"

#include "rest_json_client.h"

RestJsonClient::RestJsonClient()
  : lastFetchMs_(static_cast<unsigned long>(-static_cast<long>(RATE_LIMIT_MS))) {
  client_.setInsecure();
}

void RestJsonClient::resetRateLimit() {
  // pretend we just made the last fetch RATE_LIMIT_MS ago
  lastFetchMs_ = millis() - static_cast<unsigned long>(-static_cast<long>(RATE_LIMIT_MS));
}

std::unique_ptr<DynamicJsonDocument>
RestJsonClient::getJson(String const &url) {
  // enforce a basic rate limit to prevent runaway software from making bursts
  // of API calls (looks like DoS and get's our API key turned off ...)
  unsigned long now_ms = millis();
  if (now_ms - lastFetchMs_ < RATE_LIMIT_MS) {
    DEBUG_PRINTLN("SkyStrip: RestJsonClient::getJson: RATE LIMITED");
    return nullptr;
  }
  lastFetchMs_ = now_ms;

  HTTPClient https;
  // Begin request; must use Arduino String
  if (!https.begin(client_, url)) {
    https.end();
    DEBUG_PRINTLN(String(F("SkyStrip: RestJsonClient::getJson: trouble initiating request")));
    return nullptr;
  }
  int code = https.GET();
  if (code <= 0) {
    https.end();
    DEBUG_PRINTLN(String(F("SkyStrip: RestJsonClient::getJson: https get error code: ")) + code);
    return nullptr;
  }

  int len = https.getSize();
  size_t capacity = (len > 0 ? len : 1024) * 2;  // fallback if size is unknown
  DEBUG_PRINTF("SkyStrip: RestJsonClient::getJson: allocating %u bytes, free heap before deserialization: %u\n", capacity, ESP.getFreeHeap());
  auto doc = ::make_unique<DynamicJsonDocument>(capacity);
  auto err = deserializeJson(*doc, https.getStream());
  https.end();
  if (err) {
    DEBUG_PRINTF("SkyStrip: RestJsonClient::getJson: deserialization error: %s; free heap: %u\n", err.c_str(), ESP.getFreeHeap());
    return nullptr;
  }
  return doc;
}

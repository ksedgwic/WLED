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
  String payload = https.getString();
  https.end();
  // DEBUG_PRINTLN(String(F("SkyStrip: RestJsonClient::getJson: saw ")) + payload);

  // Allocate a document with twice the payload size for safety
  auto doc = ::make_unique<DynamicJsonDocument>(payload.length() * 2);
  auto err = deserializeJson(*doc, payload);
  if (err) {
    DEBUG_PRINTLN(String(F("SkyStrip: RestJsonClient::getJson: deserialization error: "))
                  + err.c_str());
    return nullptr;
  }
  return doc;
}

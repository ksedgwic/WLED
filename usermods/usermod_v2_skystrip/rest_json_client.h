#pragma once

#include <memory>

#include "WiFiClientSecure.h"
#include "wled.h"

#if defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266HTTPClient.h>
#else
#include <HTTPClient.h>
#endif

class RestJsonClient {
public:
  RestJsonClient();
  virtual ~RestJsonClient() = default;

  std::unique_ptr<DynamicJsonDocument> getJson(String const &url);

  void resetRateLimit();

protected:
  static constexpr unsigned RATE_LIMIT_MS = 10u * 1000u; // 10 seconds

private:
  WiFiClientSecure client_;
  unsigned long lastFetchMs_;
};

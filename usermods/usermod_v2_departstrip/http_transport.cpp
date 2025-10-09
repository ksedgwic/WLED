#include "http_transport.h"

namespace departstrip {
namespace net {

WiFiClient* HttpTransport::begin(const String& url, uint32_t timeoutMs, bool& usedSecure) {
  bool isHttps = url.startsWith(F("https://")) || url.startsWith(F("HTTPS://"));
  usedSecure = false;

  client_.setTimeout(timeoutMs);
  client_.stop();

#if defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_ARCH_ESP32)
  if (isHttps) {
    usedSecure = true;
    clientSecure_.stop();
    clientSecure_.setTimeout(timeoutMs);
    clientSecure_.setInsecure();
    return &clientSecure_;
  }
#else
  (void)isHttps;
#endif

  return &client_;
}

void HttpTransport::end(bool usedSecure) {
#if defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_ARCH_ESP32)
  if (usedSecure) {
    clientSecure_.stop();
    return;
  }
#else
  (void)usedSecure;
#endif
  client_.stop();
}

} // namespace net
} // namespace departstrip


#include <cstdint>
#include <ctime>
#include <memory>

#include "usermod_v2_skystrip.h"
#include "interfaces.h"
#include "time_util.h"

#include "skymodel.h"
#include "open_weather_map_source.h"
#include "temperature_view.h"

const char CFG_NAME[] = "SkyStrip";
const char CFG_ENABLED[] = "Enabled";

static SkyStrip skystrip_usermod;
REGISTER_USERMOD(skystrip_usermod);

// Don't handle the loop function for SAFETY_DELAY_MSECS.  If we've
// coded a deadlock or crash in the loop handler this will give us a
// chance to offMode the device so we can use the OTA update to fix
// the problem.
const time_t SAFETY_DELAY_MSECS = 10 * 1000;

// runs before readFromConfig() and setup()
SkyStrip::SkyStrip() {
  DEBUG_PRINTLN(F("SkyStrip::SkyStrip CTOR"));
  sources_.push_back(::make_unique<OpenWeatherMapSource>());
  model_ = ::make_unique<SkyModel>();
  views_.push_back(::make_unique<TemperatureView>());
}

void SkyStrip::setup() {
  // NOTE - it's a really bad idea to crash or deadlock in this
  // method; you won't be able to use OTA update and will have to
  // resort to a serial connection to unbrick your controller ...

  // NOTE - if you are using UDP logging the DEBUG_PRINTLNs in this
  // routine will likely not show up because this is prior to WiFi
  // being up.

  DEBUG_PRINTLN(F("SkyStrip::setup starting"));

  uint32_t now_ms = millis();
  safeToStart_ = now_ms + SAFETY_DELAY_MSECS;

  // Serial.begin(115200);

  // Print version number
  DEBUG_PRINT(F("SkyStrip version: "));
  DEBUG_PRINTLN(SKYSTRIP_VERSION);

  // Start a nice chase so we know its booting
  showBooting();

  state_ = SkyStripState::Setup;

  DEBUG_PRINTLN(F("SkyStrip::setup finished"));
}

void SkyStrip::loop() {
  uint32_t now_ms = millis();

  // init edge baselines once
  if (!edgeInit_) {
    lastOff_     = offMode;
    lastEnabled_ = enabled_;
    edgeInit_    = true;
  }

  time_t const now = time_util::time_now_utc();

  // defer a short bit after reboot
  if (state_ == SkyStripState::Setup) {
    if (now_ms < safeToStart_) {
      return;
    } else {
      DEBUG_PRINTLN(F("SkyStrip::loop SkyStripState is Running"));
      state_ = SkyStripState::Running;
      doneBooting();
      resetSources(now); // load right away
    }
  }

  // detect OFF->ON and disabled->enabled edges
  const bool becameOn       = (lastOff_ && !offMode);
  const bool becameEnabled  = (!lastEnabled_ && enabled_);
  if (becameOn || becameEnabled) {
    resetSources(now);
  }
  lastOff_     = offMode;
  lastEnabled_ = enabled_;

  // make sure we are enabled, on, and ready
  if (!enabled_ || offMode || strip.isUpdating()) return;

  // check the sources for updates, apply to model if found
  for (auto &source : sources_) {
    if (auto frmsrc = source->fetch(now)) {
      // this happens relatively infrequently, once an hour
      model_->update(now, std::move(*frmsrc));
    }
  }
}

void SkyStrip::handleOverlayDraw() {
    // this happens a hundred times a second
  time_t now = time_util::time_now_utc();
  for (auto &view : views_) {
    view->view(now, *model_);
  }
}

// called by WLED when settings are saved
void SkyStrip::addToConfig(JsonObject& root) {
  JsonObject top = root.createNestedObject(FPSTR(CFG_NAME));

  // write our state
  top[FPSTR(CFG_ENABLED)] = enabled_;

  // write the sources
  for (auto& src : sources_) {
    JsonObject sub = top.createNestedObject(src->configKey());
    src->addToConfig(sub);
  }

  // write the views
  for (auto& vw : views_) {
    JsonObject sub = top.createNestedObject(vw->configKey());
    vw->addToConfig(sub);
  }
}

// called by WLED when settings are restored
bool SkyStrip::readFromConfig(JsonObject& root) {
  JsonObject top = root[FPSTR(CFG_NAME)];
  if (top.isNull()) return false;

  bool ok = true;

  // It is not safe to make API calls during startup
  bool  startup_complete = state_ == SkyStripState::Running;

  // read our state
  ok &= getJsonValue(top[FPSTR(CFG_ENABLED)], enabled_, false);

  // read the sources
  for (auto& src : sources_) {
    JsonObject sub = top[src->configKey()];
    ok &= src->readFromConfig(sub, startup_complete);
  }

  // read the views
  for (auto& vw : views_) {
    JsonObject sub = top[vw->configKey()];
    ok &= vw->readFromConfig(sub, startup_complete);
  }

    // if safe (we are running) load from API right away
  if (startup_complete) {
    time_t const now = time_util::time_now_utc();
    resetSources(now);
  }

  return ok;
}

void SkyStrip::showBooting() {
  Segment& seg = strip.getMainSegment();
  seg.setMode(28); // Set to chase
  seg.speed = 200;
  // seg.intensity = 255; // preserve user's settings via webapp
  seg.setPalette(128);
  seg.setColor(0, 0x404060);
  seg.setColor(1, 0x000000);
  seg.setColor(2, 0x303040);
}

void SkyStrip::doneBooting() {
  Segment& seg = strip.getMainSegment();
  seg.freeze = true;    // stop any further segment animation
  seg.setMode(0);       // static palette/color mode
  // seg.intensity = 255;  // preserve user's settings via webapp
}

void SkyStrip::resetSources(std::time_t now) {
  char nowBuf[20];
  time_util::fmt_local(nowBuf, sizeof(nowBuf), now);
  DEBUG_PRINTF("SkyStrip::ResetSources at %s\n", nowBuf);

  for (auto &src : sources_) src->reset(now);
}

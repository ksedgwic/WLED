#include <cstdint>
#include <ctime>
#include <memory>

#include "usermod_v2_bartdepart2.h"
#include "interfaces.h"
#include "util.h"

#include "bart_station_model.h"
#include "legacy_bart_source.h"
#include "platform_view.h"

const char CFG_NAME[] = "BartDepart2";
const char CFG_ENABLED[] = "Enabled";
const char CFG_DBG_PIXEL_INDEX[] = "DebugPixelIndex";

static BartDepart2 bartdepart2_usermod;
REGISTER_USERMOD(bartdepart2_usermod);

const time_t SAFETY_DELAY_MSECS = 10 * 1000;

BartDepart2::BartDepart2() {
  sources_.push_back(::make_unique<LegacyBartSource>());
  model_ = ::make_unique<BartStationModel>();
  views_.push_back(::make_unique<PlatformView>("1"));
  views_.push_back(::make_unique<PlatformView>("2"));
  views_.push_back(::make_unique<PlatformView>("3"));
  views_.push_back(::make_unique<PlatformView>("4"));
}

void BartDepart2::setup() {
  DEBUG_PRINTLN(F("BartDepart2::setup starting"));
  uint32_t now_ms = millis();
  safeToStart_ = now_ms + SAFETY_DELAY_MSECS;
  showBooting();
  state_ = BartDepart2State::Setup;
  DEBUG_PRINTLN(F("BartDepart2::setup finished"));
}

void BartDepart2::loop() {
  uint32_t now_ms = millis();
  if (!edgeInit_) {
    lastOff_ = offMode;
    lastEnabled_ = enabled_;
    edgeInit_ = true;
  }

  time_t const now = util::time_now_utc();

  if (state_ == BartDepart2State::Setup) {
    if (now_ms < safeToStart_) return;
    state_ = BartDepart2State::Running;
    doneBooting();
    reloadSources(now);
  }

  bool becameOn = (lastOff_ && !offMode);
  bool becameEnabled = (!lastEnabled_ && enabled_);
  if (becameOn || becameEnabled) {
    reloadSources(now);
  }
  lastOff_ = offMode;
  lastEnabled_ = enabled_;

  if (!enabled_ || offMode || strip.isUpdating()) return;

  for (auto& src : sources_) {
    if (auto data = src->fetch(now)) {
      model_->update(now, std::move(*data));
    }
    if (auto hist = src->checkhistory(now, model_->oldest())) {
      model_->update(now, std::move(*hist));
    }
  }
}

void BartDepart2::handleOverlayDraw() {
  time_t now = util::time_now_utc();
  for (auto& view : views_) {
    view->view(now, *model_, dbgPixelIndex_);
  }
}

void BartDepart2::addToConfig(JsonObject& root) {
  JsonObject top = root.createNestedObject(FPSTR(CFG_NAME));
  top[FPSTR(CFG_ENABLED)] = enabled_;
  top[FPSTR(CFG_DBG_PIXEL_INDEX)] = dbgPixelIndex_;
  for (auto& src : sources_) {
    JsonObject sub = top.createNestedObject(src->configKey());
    src->addToConfig(sub);
  }
  for (auto& vw : views_) {
    JsonObject sub = top.createNestedObject(vw->configKey());
    vw->addToConfig(sub);
  }
}

void BartDepart2::appendConfigData(Print& s) {
  for (auto& src : sources_) {
    src->appendConfigData(s);
  }

  for (auto& vw : views_) {
    vw->appendConfigData(s, model_.get());
  }
}

bool BartDepart2::readFromConfig(JsonObject& root) {
  JsonObject top = root[FPSTR(CFG_NAME)];
  if (top.isNull()) return false;

  bool ok = true;
  bool invalidate_history = false;
  bool startup_complete = state_ == BartDepart2State::Running;

  ok &= getJsonValue(top[FPSTR(CFG_ENABLED)], enabled_, false);
  ok &= getJsonValue(top[FPSTR(CFG_DBG_PIXEL_INDEX)], dbgPixelIndex_, -1);

  for (auto& src : sources_) {
    JsonObject sub = top[src->configKey()];
    ok &= src->readFromConfig(sub, startup_complete, invalidate_history);
  }
  for (auto& vw : views_) {
    JsonObject sub = top[vw->configKey()];
    ok &= vw->readFromConfig(sub, startup_complete, invalidate_history);
  }

  if (invalidate_history) {
    model_->platforms.clear();
    if (startup_complete) reloadSources(util::time_now_utc());
  }

  return ok;
}

void BartDepart2::showBooting() {
  Segment& seg = strip.getMainSegment();
  seg.setMode(28);
  seg.speed = 200;
  seg.setPalette(128);
  seg.setColor(0, 0x404060);
  seg.setColor(1, 0x000000);
  seg.setColor(2, 0x303040);
}

void BartDepart2::doneBooting() {
  Segment& seg = strip.getMainSegment();
  seg.freeze = true;
  seg.setMode(0);
}

void BartDepart2::reloadSources(std::time_t now) {
  for (auto& src : sources_) src->reload(now);
}

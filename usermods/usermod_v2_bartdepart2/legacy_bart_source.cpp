#include "legacy_bart_source.h"
#include "util.h"

LegacyBartSource::LegacyBartSource() {
  client_.setInsecure();
}

void LegacyBartSource::reload(std::time_t now) {
  nextFetch_ = now;
  backoffMult_ = 1;
}

static String composeUrl(const String& base, const String& key, const String& station) {
  String url = base;
  url += "&key="; url += key;
  url += "&orig="; url += station;
  return url;
}

std::unique_ptr<BartStationModel> LegacyBartSource::fetch(std::time_t now) {
  if (now == 0 || now < nextFetch_) return nullptr;

  String url = composeUrl(apiBase_, apiKey_, apiStation_);
  https_.begin(client_, url);
  int httpCode = https_.GET();
  if (httpCode <= 0) {
    https_.end();
    nextFetch_ = now + updateSecs_ * backoffMult_;
    if (backoffMult_ < 16) backoffMult_ *= 2;
    return nullptr;
  }
  String payload = https_.getString();
  https_.end();

  size_t jsonSz = payload.length() * 2;
  DynamicJsonDocument doc(jsonSz);
  auto err = deserializeJson(doc, payload);
  if (err) {
    nextFetch_ = now + updateSecs_ * backoffMult_;
    if (backoffMult_ < 16) backoffMult_ *= 2;
    return nullptr;
  }

  JsonObject root = doc["root"].as<JsonObject>();
  if (root.isNull()) {
    nextFetch_ = now + updateSecs_ * backoffMult_;
    if (backoffMult_ < 16) backoffMult_ *= 2;
    return nullptr;
  }

  std::unique_ptr<BartStationModel> model(new BartStationModel());
  for (const String& pid : platformIds()) {
    if (pid.isEmpty()) continue;
    TrainPlatformModel tp(pid);
    tp.update(root);
    model->platforms.push_back(std::move(tp));
  }

  nextFetch_ = now + updateSecs_;
  backoffMult_ = 1;
  return model;
}

void LegacyBartSource::addToConfig(JsonObject& root) {
  root["UpdateSecs"] = updateSecs_;
  root["ApiBase"] = apiBase_;
  root["ApiKey"] = apiKey_;
  root["ApiStation"] = apiStation_;
  root["Platform1"] = platform1Id_;
  root["Platform2"] = platform2Id_;
  root["Platform3"] = platform3Id_;
  root["Platform4"] = platform4Id_;
}

bool LegacyBartSource::readFromConfig(JsonObject& root, bool startup_complete, bool& invalidate_history) {
  bool ok = true;
  ok &= getJsonValue(root["UpdateSecs"], updateSecs_, 60);
  ok &= getJsonValue(root["ApiBase"], apiBase_, apiBase_);
  ok &= getJsonValue(root["ApiKey"], apiKey_, apiKey_);
  ok &= getJsonValue(root["ApiStation"], apiStation_, apiStation_);
  ok &= getJsonValue(root["Platform1"], platform1Id_, platform1Id_);
  ok &= getJsonValue(root["Platform2"], platform2Id_, platform2Id_);
  ok &= getJsonValue(root["Platform3"], platform3Id_, platform3Id_);
  ok &= getJsonValue(root["Platform4"], platform4Id_, platform4Id_);
  invalidate_history = true;
  return ok;
}

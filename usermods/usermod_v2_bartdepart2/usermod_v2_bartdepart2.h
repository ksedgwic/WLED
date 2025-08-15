#pragma once
#include <memory>

#include "interfaces.h"
#include "wled.h"

#define BARTDEPART2_VERSION "0.0.1"

class BartStationModel;
class LegacyBartSource;
class PlatformView;

enum class BartDepart2State {
  Initial,
  Setup,
  Running
};

class BartDepart2 : public Usermod {
private:
  bool enabled_ = false;
  int16_t dbgPixelIndex_ = -1;
  BartDepart2State state_ = BartDepart2State::Initial;
  uint32_t safeToStart_ = 0;
  bool edgeInit_ = false;
  bool lastOff_ = false;
  bool lastEnabled_ = false;

  std::vector<std::unique_ptr<IDataSourceT<BartStationModel>>> sources_;
  std::unique_ptr<BartStationModel> model_;
  std::vector<std::unique_ptr<IDataViewT<BartStationModel>>> views_;

public:
  BartDepart2();
  ~BartDepart2() override = default;
  void setup() override;
  void loop() override;
  void handleOverlayDraw() override;
  void addToConfig(JsonObject& obj) override;
  bool readFromConfig(JsonObject& obj) override;
  uint16_t getId() override { return USERMOD_ID_BARTDEPART; }

  inline void enable(bool en) { enabled_ = en; }
  inline bool isEnabled() { return enabled_; }

protected:
  void showBooting();
  void doneBooting();
  void reloadSources(std::time_t now);
};

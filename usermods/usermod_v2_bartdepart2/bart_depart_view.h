#pragma once

#include "interfaces.h"
#include "bart_model.h"

class BartDepartView : public IDataViewT<BartModel> {
private:
  uint16_t updateSecs_ = 60;
public:
  BartDepartView() = default;
  void view(std::time_t now, const BartModel& model, int16_t dbgPixelIndex) override;
  void addToConfig(JsonObject& root) override {}
  bool readFromConfig(JsonObject& root, bool startup_complete, bool& invalidate_history) override { return true; }
  const char* configKey() const override { return "BartDepartView"; }
  std::string name() const override { return "BartDepartView"; }
};

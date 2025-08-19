#pragma once

#include "interfaces.h"
#include "skymodel.h"

class SkyModel;

class TestPatternView : public IDataViewT<SkyModel> {
public:
  TestPatternView();
  ~TestPatternView() override = default;

  void view(time_t now, SkyModel const & model, int16_t dbgPixelIndex) override;
  std::string name() const override { return "TP"; }

  void addToConfig(JsonObject& subtree) override;
  void appendConfigData(Print& s) override;
  bool readFromConfig(JsonObject& subtree,
                      bool startup_complete,
                      bool& invalidate_history) override;
  const char* configKey() const override { return "TestPatternView"; }

private:
  int16_t segId_;
  float startHue_, startSat_, startVal_;
  float endHue_, endSat_, endVal_;
};

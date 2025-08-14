#pragma once

#include "interfaces.h"
#include "skymodel.h"

class SkyModel;

class WindView : public IDataViewT<SkyModel> {
public:
  WindView();
  ~WindView() override = default;

  void view(time_t now, SkyModel const & model, int16_t dbgPixelIndex) override;
  std::string name() const override { return "WV"; }

  void addToConfig(JsonObject& subtree) override;
  bool readFromConfig(JsonObject& subtree,
                      bool startup_complete,
                      bool& invalidate_history) override;
  const char* configKey() const override { return "WindView"; }

private:
  int16_t segId_;
};

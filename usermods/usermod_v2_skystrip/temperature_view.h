#pragma once

#include "interfaces.h"
#include "skymodel.h"

class SkyModel;

class TemperatureView : public IDataViewT<SkyModel> {
public:
  TemperatureView();
  ~TemperatureView() override = default;

  // IDataViewT<SkyModel>
  void view(time_t now, SkyModel const & model) override;
  std::string name() const override { return "TV"; }

  // IConfigurable
  void addToConfig(JsonObject& subtree) override;
  bool readFromConfig(JsonObject& subtree) override;
  const char* configKey() const override { return "TemperatureView"; }

private:
  int16_t segId_; // -1 means disabled
};

#pragma once

#include "interfaces.h"
#include "skymodel.h"

class SkyModel;

class CloudView : public IDataViewT<SkyModel> {
public:
  CloudView();
  ~CloudView() override = default;

  void view(time_t now, SkyModel const & model) override;
  std::string name() const override { return "CV"; }

  void addToConfig(JsonObject& subtree) override;
  bool readFromConfig(JsonObject& subtree, bool startup_complete) override;
  const char* configKey() const override { return "CloudView"; }

private:
  int16_t segId_;
};

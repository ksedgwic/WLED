#pragma once

#include "wled.h"

#include <ctime>
#include <vector>
#include <deque>

#include "train_color.h"

class TrainPlatformModel {
public:
  // estimated time of departure
  struct ETD {
    time_t        estDep;
    TrainColor    color;
  };

  struct ETDBatch {
    time_t           apiTs; // tstamp from the API response
    time_t           ourTs; // our local timestamp
    std::vector<ETD> etds;
  };

  explicit TrainPlatformModel(const String& platformId)
    : platformId_(platformId) {}

  // update the departure estimates based on the json object
  void update(const JsonObject& root);

  // display the departure estimates on the LED strip segment
  void display(time_t now, size_t segment, uint16_t updateSecs) const;

  // merge another platform's history into this one
  void merge(const TrainPlatformModel& other);

  // oldest sample timestamp
  time_t oldest() const;

  // expose platform id
  const String& platformId() const { return platformId_; }

  String toString() const;

private:
  String platformId_;	// disable if empty
  std::deque<ETDBatch>      history_;

  time_t      parseHeaderTimestamp(const char* date, const char* time) const;
};

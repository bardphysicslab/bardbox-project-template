#pragma once

#include "BardBoxNetworkConfig.h"

namespace bbcore {

// M1: configuration and status only. Connection, delivery and acknowledgement
// are a later milestone, so an enabled network reports "not_implemented".
class BardBoxNetwork {
 public:
  enum class Status { Disabled, NotImplemented };

  void begin() { status_ = BARDBOX_NETWORK_ENABLED ? Status::NotImplemented : Status::Disabled; }
  Status status() const { return status_; }
  const char *statusName() const {
    return status_ == Status::Disabled ? "disabled" : "not_implemented";
  }

 private:
  Status status_ = Status::Disabled;
};

}  // namespace bbcore

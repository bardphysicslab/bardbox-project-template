#pragma once

#include <Arduino.h>

namespace bardbox {

// Transport-independent bounded recovery policy. The node implementation owns
// the actual Wi-Fi/client restart and optional reset hook.
struct TransportRecoveryPolicy {
  uint8_t reconnectAfterFailures = 1;
  uint8_t reinitializeAfterFailures = 3;
  uint8_t restartAfterFailures = 12;
  uint32_t retryBackoffMs = 30000;
};

enum class TransportRecoveryAction : uint8_t {
  None, Reconnect, ReinitializeClient, RestartDevice
};

struct TransportHealthSnapshot {
  uint32_t consecutiveFailures = 0;
  uint32_t totalFailures = 0;
  uint32_t successfulAcknowledgements = 0;
  uint32_t lastAttemptMs = 0;
  uint32_t lastSuccessMs = 0;
};

// Call noteResult once an attempt has either received the protocol's positive
// acknowledgement (HTTP Web Nodes: any 2xx) or conclusively failed. Never use
// this policy to discard unacknowledged data.
class TransportRecoveryTracker {
 public:
  explicit TransportRecoveryTracker(TransportRecoveryPolicy policy = {}) : policy_(policy) {}

  void noteResult(bool acknowledged, uint32_t nowMs) {
    snapshot_.lastAttemptMs = nowMs;
    if (acknowledged) {
      snapshot_.consecutiveFailures = 0;
      snapshot_.successfulAcknowledgements++;
      snapshot_.lastSuccessMs = nowMs;
    } else {
      snapshot_.consecutiveFailures++;
      snapshot_.totalFailures++;
    }
  }

  bool retryDue(uint32_t nowMs) const {
    return !snapshot_.consecutiveFailures ||
      nowMs - snapshot_.lastAttemptMs >= policy_.retryBackoffMs;
  }

  TransportRecoveryAction nextAction() const {
    const uint32_t failures = snapshot_.consecutiveFailures;
    if (failures >= policy_.restartAfterFailures) return TransportRecoveryAction::RestartDevice;
    if (failures >= policy_.reinitializeAfterFailures) return TransportRecoveryAction::ReinitializeClient;
    if (failures >= policy_.reconnectAfterFailures) return TransportRecoveryAction::Reconnect;
    return TransportRecoveryAction::None;
  }

  const TransportHealthSnapshot &snapshot() const { return snapshot_; }

 private:
  TransportRecoveryPolicy policy_;
  TransportHealthSnapshot snapshot_;
};

} // namespace bardbox

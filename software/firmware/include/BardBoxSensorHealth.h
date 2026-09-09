#pragma once

#include <Arduino.h>
#include <math.h>

namespace bardbox {

enum class SensorHealthState : uint8_t { Unknown, Healthy, Degraded, Recovering, Failed };
enum class SensorFaultKind : uint8_t { None, Transport, InvalidSample, Stale, DeviceFault };

struct RecoveryPolicy {
  uint8_t recoverAfterConsecutiveFaults = 3;
  uint8_t failAfterRecoveryFailures = 3;
  uint32_t recoveryCooldownMs = 1000;
  uint32_t staleAfterMs = 0; // 0 disables stale-age tracking
};

struct SensorHealthSnapshot {
  SensorHealthState state = SensorHealthState::Unknown;
  SensorFaultKind lastFault = SensorFaultKind::None;
  uint32_t goodSamples = 0;
  uint32_t consecutiveFaults = 0;
  uint32_t recoveryAttempts = 0;
  uint32_t recoveryFailures = 0;
  uint32_t lastGoodMs = 0;
  uint32_t lastFaultMs = 0;
  uint32_t lastRecoveryMs = 0;
};

// Sensor-agnostic policy only. Drivers own their own ranges, units,
// rate-of-change rules, transport reinitialization, and safe state.
class SensorHealthTracker {
 public:
  explicit SensorHealthTracker(RecoveryPolicy policy = {}) : policy_(policy) {}

  void noteGood(uint32_t nowMs) {
    snapshot_.state = SensorHealthState::Healthy;
    snapshot_.lastFault = SensorFaultKind::None;
    snapshot_.goodSamples++;
    snapshot_.consecutiveFaults = 0;
    snapshot_.recoveryFailures = 0;
    snapshot_.lastGoodMs = nowMs;
  }

  void noteFault(SensorFaultKind kind, uint32_t nowMs) {
    snapshot_.lastFault = kind;
    snapshot_.consecutiveFaults++;
    snapshot_.lastFaultMs = nowMs;
    snapshot_.state = SensorHealthState::Degraded;
  }

  bool recoveryDue(uint32_t nowMs) const {
    return snapshot_.consecutiveFaults >= policy_.recoverAfterConsecutiveFaults &&
      nowMs - snapshot_.lastRecoveryMs >= policy_.recoveryCooldownMs;
  }

  void noteRecoveryAttempt(uint32_t nowMs) {
    snapshot_.state = SensorHealthState::Recovering;
    snapshot_.recoveryAttempts++;
    snapshot_.lastRecoveryMs = nowMs;
  }

  void noteRecoveryResult(bool recovered, uint32_t nowMs) {
    if (recovered) {
      snapshot_.state = SensorHealthState::Healthy;
      snapshot_.consecutiveFaults = 0;
      snapshot_.recoveryFailures = 0;
      snapshot_.lastGoodMs = nowMs;
    } else {
      snapshot_.recoveryFailures++;
      snapshot_.state = snapshot_.recoveryFailures >= policy_.failAfterRecoveryFailures
        ? SensorHealthState::Failed : SensorHealthState::Degraded;
    }
  }

  bool stale(uint32_t nowMs) const {
    return policy_.staleAfterMs && snapshot_.lastGoodMs &&
      nowMs - snapshot_.lastGoodMs > policy_.staleAfterMs;
  }

  const SensorHealthSnapshot &snapshot() const { return snapshot_; }

 private:
  RecoveryPolicy policy_;
  SensorHealthSnapshot snapshot_;
};

} // namespace bardbox

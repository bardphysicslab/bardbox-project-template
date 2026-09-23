#pragma once

#include <Arduino.h>
#include <math.h>

namespace bardbox {

enum class SensorFaultKind : uint8_t {
    None = 0,
    Transport,
    InvalidSample,
    Stale,
    DeviceFault
};

enum class SensorHealthState : uint8_t {
    Unknown = 0,
    Healthy,
    Degraded,
    Recovering,
    Failed
};

struct SensorRecoveryPolicy {
    uint8_t recoverAfterConsecutiveFaults = 3;
    uint8_t failAfterRecoveryFailures = 3;
    uint32_t recoveryCooldownMs = 1000;
    uint32_t staleAfterMs = 0; // 0 disables stale detection.
};

struct SensorValueRule {
    const char *units = "";
    bool enforceMin = false;
    bool enforceMax = false;
    float minValue = 0.0f;
    float maxValue = 0.0f;
    bool enforceMaxDelta = false;
    float maxDelta = 0.0f;
    bool enforceMaxRate = false;
    float maxRatePerSecond = 0.0f;
};

inline bool validateSensorValue(float value,
                                const SensorValueRule &rule,
                                bool havePrevious = false,
                                float previousValue = NAN,
                                uint32_t elapsedMs = 0)
{
    if (isnan(value) || isinf(value)) return false;
    if (rule.enforceMin && value < rule.minValue) return false;
    if (rule.enforceMax && value > rule.maxValue) return false;

    if (havePrevious && !isnan(previousValue) && !isinf(previousValue)) {
        const float delta = fabsf(value - previousValue);
        if (rule.enforceMaxDelta && delta > rule.maxDelta) return false;
        if (rule.enforceMaxRate && elapsedMs > 0) {
            const float rate = delta / (static_cast<float>(elapsedMs) / 1000.0f);
            if (rate > rule.maxRatePerSecond) return false;
        }
    }

    return true;
}

struct SensorHealthSnapshot {
    SensorHealthState state = SensorHealthState::Unknown;
    SensorFaultKind lastFault = SensorFaultKind::None;
    uint32_t totalGoodSamples = 0;
    uint32_t totalFaults = 0;
    uint32_t totalRecoveryAttempts = 0;
    uint16_t consecutiveFaults = 0;
    uint8_t consecutiveRecoveryFailures = 0;
    uint32_t lastGoodMs = 0;
    uint32_t lastFaultMs = 0;
    uint32_t lastRecoveryAttemptMs = 0;
};

class SensorHealthTracker {
public:
    explicit SensorHealthTracker(const SensorRecoveryPolicy &policy = SensorRecoveryPolicy())
        : policy_(policy) {}

    void setPolicy(const SensorRecoveryPolicy &policy) { policy_ = policy; }
    const SensorRecoveryPolicy &policy() const { return policy_; }

    void noteGood(uint32_t nowMs) {
        snapshot_.state = SensorHealthState::Healthy;
        snapshot_.lastFault = SensorFaultKind::None;
        snapshot_.totalGoodSamples++;
        snapshot_.consecutiveFaults = 0;
        snapshot_.consecutiveRecoveryFailures = 0;
        snapshot_.lastGoodMs = nowMs;
    }

    void noteFault(SensorFaultKind kind, uint32_t nowMs) {
        snapshot_.lastFault = kind;
        snapshot_.totalFaults++;
        snapshot_.consecutiveFaults++;
        snapshot_.lastFaultMs = nowMs;
        if (snapshot_.state != SensorHealthState::Recovering && snapshot_.state != SensorHealthState::Failed) {
            snapshot_.state = SensorHealthState::Degraded;
        }
    }

    bool isStale(uint32_t nowMs) const {
        return policy_.staleAfterMs > 0 && snapshot_.lastGoodMs > 0 &&
               static_cast<uint32_t>(nowMs - snapshot_.lastGoodMs) > policy_.staleAfterMs;
    }

    bool shouldAttemptRecovery(uint32_t nowMs) const {
        if (snapshot_.consecutiveFaults < policy_.recoverAfterConsecutiveFaults) return false;
        if (snapshot_.lastRecoveryAttemptMs == 0) return true;
        return static_cast<uint32_t>(nowMs - snapshot_.lastRecoveryAttemptMs) >= policy_.recoveryCooldownMs;
    }

    void noteRecoveryAttempt(uint32_t nowMs) {
        snapshot_.state = SensorHealthState::Recovering;
        snapshot_.totalRecoveryAttempts++;
        snapshot_.lastRecoveryAttemptMs = nowMs;
    }

    void noteRecoverySuccess(uint32_t nowMs) {
        snapshot_.state = SensorHealthState::Degraded;
        snapshot_.consecutiveFaults = 0;
        snapshot_.consecutiveRecoveryFailures = 0;
        snapshot_.lastRecoveryAttemptMs = nowMs;
    }

    void noteRecoveryFailure(uint32_t nowMs) {
        snapshot_.consecutiveRecoveryFailures++;
        snapshot_.lastRecoveryAttemptMs = nowMs;
        snapshot_.state = snapshot_.consecutiveRecoveryFailures >= policy_.failAfterRecoveryFailures
                              ? SensorHealthState::Failed
                              : SensorHealthState::Degraded;
    }

    const SensorHealthSnapshot &snapshot() const { return snapshot_; }

private:
    SensorRecoveryPolicy policy_;
    SensorHealthSnapshot snapshot_;
};

} // namespace bardbox

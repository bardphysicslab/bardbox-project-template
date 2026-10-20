#pragma once

#include <math.h>
#include <stdint.h>

// Optional per-measurement value check, preserved from the retired root
// firmware/include/BardBoxSensorHealth.h (see Git history). It is standalone:
// it does not depend on, or change, BardBoxSensorHealth.h. Each sensor or
// measurement definition supplies its own rule; the shared layer never
// hard-codes a physical range.

namespace bardbox {

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

} // namespace bardbox

#pragma once

#include <Arduino.h>
#include <Wire.h>

namespace bardbox {

struct I2CRecoveryConfig {
    bool enabled = true;
    uint16_t settleDelayMs = 10;
};

// Additive helper: it does not replace or subclass Wire.h. Projects opt in by
// calling this only after their health policy requests transport recovery.
inline bool recoverI2CBus(TwoWire &wire = Wire,
                          const I2CRecoveryConfig &config = I2CRecoveryConfig())
{
    if (!config.enabled) return false;

#if defined(ARDUINO_ARCH_ESP32)
    wire.end();
    delay(config.settleDelayMs);
    return wire.begin();
#else
    // Keep the shared layer portable and backwards-compatible. On platforms
    // without a known-safe generic Wire reset, the sensor driver should supply
    // a platform-specific recovery hook instead of guessing bus pins/state.
    (void)wire;
    return false;
#endif
}

} // namespace bardbox

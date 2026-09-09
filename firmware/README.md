# BardBox firmware baseline

New BardBox firmware should include the shared sensor-health layer in `firmware/include/`.

The common layer is deliberately sensor-agnostic. It tracks health, stale data, consecutive failures, recovery escalation, and diagnostics. It does **not** decide whether a specific numeric reading is physically valid. Each sensor/measurement definition owns its own validation configuration: units, expected min/max, rate-of-change limits, checksum or device-fault interpretation, and any other device-specific rules.

Transport helpers are optional. If firmware uses I2C it may include `BardBoxI2CRecovery.h`; UART, SPI, or other transports should follow the same pattern. These helpers sit above normal Arduino transports and must not fork or replace `Wire.h`, `HardwareSerial`, etc.

Backwards compatibility is mandatory: adopting the layer must not require changes to existing node configuration, UID/protocol semantics, payload fields, command names, or deployed data formats unless a separately approved protocol migration explicitly says otherwise.

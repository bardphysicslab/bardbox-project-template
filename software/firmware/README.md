# Firmware

Firmware development for BardBox projects uses VS Code with PlatformIO.

The Arduino framework is acceptable, but it should be used through PlatformIO
rather than the Arduino IDE. Keep deployment firmware in this structure:

```text
software/firmware/
  platformio.ini
  src/main.cpp
  include/
  lib/
```

Layout and integrated firmware milestones ([#36](https://github.com/bardphysicslab/bardbox-project-template/issues/36)):
the BardBox modules in `lib/` now contain code, host-tested with simulated
values and built for the supported board, but not run on hardware. This is
progress toward the first usable template, not its completion: recording
persists to LittleFS (host-tested with simulated storage and restarts only),
and Network delivery is host-tested through a simulated transport only. Real Wi-Fi/HTTPS and OTA are not implemented. Start in `src/main.cpp`.

```text
software/firmware/
  platformio.ini                     Feather ESP32-S3 (8 MiB, no PSRAM) environments
  partitions_bardbox_8mb_ota.csv     OTA-ready 8 MiB layout (slots reserved; OTA not implemented)
  src/main.cpp                       student start file: version, sensors, read/publish/delay
  src/SimulatedBme280.h              SIMULATED driver for the M1 demo
  include/config.example.h           tracked settings example (Serial, Identity, Network, Buffer)
  include/config.h                   your ignored local copy
  include/                           also four standalone helpers (see include/README.md)
  lib/BardBoxInterface/              student API; wires the owners together
  lib/BardBoxMeasurements/           registration, publish checks, latest-reading cache
  lib/BardBoxBuffer/                 recording interval, records, durable FIFO queue and ACK
  lib/BardBoxSerial/                 serial commands and wire forms
  lib/BardBoxNetwork/                delivery policy/mailbox and candidate JSON (fake transport only)
  test/host/                         host tests and demo (simulated)
```

Build: `pio run -e feather_s3` fails until `FIRMWARE_VERSION` is set in
`src/main.cpp` ("Please set FIRMWARE_VERSION in main.cpp."); `pio run -e
feather_s3_m1_test` builds with an explicit test version. Serial commands:
`INFO`, `HEADER`, `READ` (latest completed record), `READ_LIVE` (latest cached
readings) and `PING`; `HEADER`, `READ` and `READ_LIVE` use a v2 candidate
framing pending review (see `lib/BardBoxSerial/README.md`).

The example UID is `bb-prj-air-001`. New UIDs must follow
`bb-<site>-<type>-<instance>` with 3-letter lowercase site/type codes and a
3-digit instance. Legacy `bb-0001` style IDs are deprecated for new projects.

Firmware only reports readings. It does not decide whether a node is stale or
unavailable; the Raspberry Pi/backend tracks communication freshness and
normalizes stale or unavailable API values to `null`.

## Reliability building blocks

New firmware may reuse these sensor- and transport-agnostic headers:

- `include/BardBoxSensorHealth.h`: tracks good samples, consecutive faults,
  recovery attempts, recovery failure, and stale readings. Individual drivers
  define their own validity limits, rate-of-change rules, and recovery action.
- `include/BardBoxTransportRecovery.h`: tracks acknowledgements and returns
  a bounded recovery step: reconnect, reinitialize the network client, then
  optional device restart.
- `include/BardBoxSensorValueRule.h`: optional per-measurement value check
  (finite, min/max, maximum change and rate of change); the rule comes from the
  sensor or measurement definition.
- `include/BardBoxI2CRecovery.h`: opt-in I2C bus restart above `Wire.h`. It
  does not explicitly restore custom pins or clock, so their preservation is
  not guaranteed; review the core's behavior or supply a restore hook.

The health and transport-recovery trackers and the value check are
state/value helpers that perform no I/O; the I2C helper is the only one that
acts on hardware, and only when a project calls it. None of them is a complete
networking or sensor driver. A Web Node still must persist before upload, retry oldest-first, delete only after
a `2xx` acknowledgement, and keep sampling separate from uploads. A restart
hook is optional and must only be enabled after it has been tested on the
deployed hardware. Publish the resulting health state through additive
`INFO`/payload diagnostics.

## Local configuration

Settings live in one place: tracked `include/config.example.h` (safe example
with Serial, Identity, Network and Buffer sections) and your ignored local copy
`include/config.h`, created once with `cp -n include/config.example.h include/config.h`
and never overwritten; see [`include/README.md`](include/README.md#configuration-headers).
BardBox modules read the local `config.h`; a setting left out uses the
module's documented default. Rename an existing
private `secrets.h` locally without replacing its values. Never add credentials
to the tracked example.

Use `hardware/ecad` for KiCad and `hardware/mcad` for mechanical/enclosure files.
Keep separate firmware projects under `software/firmware/<project>`; a single
project may use this directory directly.

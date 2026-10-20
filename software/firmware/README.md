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

Starting layout ([FW-LAYOUT-1](https://github.com/bardphysicslab/bardbox/issues/31#issuecomment-6290294824),
October 20, 2026). Each folder has a role README. Folders marked *role only*
contain no code yet; the facade name `BardBoxInterface` is accepted, while
other names and all interfaces are proposals maintained in
[`bardbox/docs`](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-blueprint.md).
Start in `src/main.cpp`; split a larger project's `src/` later, only when needed.

```text
software/firmware/
  platformio.ini            build environment (unchanged)
  src/main.cpp              start file; existing simulator (unchanged)
  include/                  standard project headers; four helpers
  include/config.example.h  tracked safe config example (not consumed yet)
  include/config.h          your ignored local copy (not consumed yet)
  lib/BardBoxInterface/     beginner facade; the only BardBox entry (role only)
  lib/BardBoxSerial/        private core: serial owner (role only)
  lib/BardBoxNetwork/       private core: network delivery owner (role only)
  lib/BardBoxBuffer/        private core: durable record queue (role only)
  test/                     host and integration tests (role only)
```

The example node implements the compact BardBox device protocol:

- `INFO`
- `HEADER`
- `READ`
- optional `PING`

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
- `include/BardBoxI2CRecovery.h`: opt-in I2C bus restart above `Wire.h`; it
  restarts on default pins and clock, so custom pins/clock are not preserved.

They are policy/state helpers, not complete networking or sensor drivers. A
Web Node still must persist before upload, retry oldest-first, delete only after
a `2xx` acknowledgement, and keep sampling separate from uploads. A restart
hook is optional and must only be enabled after it has been tested on the
deployed hardware. Publish the resulting health state through additive
`INFO`/payload diagnostics.

## Local configuration

Settings live in one place: tracked `include/config.example.h` (safe example
with Serial, Network and Buffer sections) and your ignored local copy
`include/config.h`, created once with `cp -n include/config.example.h include/config.h`
and never overwritten; see [`include/README.md`](include/README.md#configuration-headers).
Neither header is consumed by the current simulator yet. Rename an existing
private `secrets.h` locally without replacing its values. Never add credentials
to the tracked example.

Use `hardware/ecad` for KiCad and `hardware/mcad` for mechanical/enclosure files.
Keep separate firmware projects under `software/firmware/<project>`; a single
project may use this directory directly.

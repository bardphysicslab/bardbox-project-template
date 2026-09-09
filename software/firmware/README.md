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

They are policy/state helpers, not complete networking or sensor drivers. A
Web Node still must persist before upload, retry oldest-first, delete only after
a `2xx` acknowledgement, and keep sampling separate from uploads. A restart
hook is optional and must only be enabled after it has been tested on the
deployed hardware. Publish the resulting health state through additive
`INFO`/payload diagnostics.

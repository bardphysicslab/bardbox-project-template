# include/

Standard PlatformIO project include folder: headers shared by this project's
`src/` files. It is not a BardBox module, and whether future helpers move into
`lib/` modules is not settled.

It holds two helpers and the tracked
[configuration example](#configuration-headers) `config.example.h`. Both helpers are policy and state trackers only: they
perform no hardware, sensor or network I/O, and the current simulator
`main.cpp` does not use them.

## Configuration headers

- `include/config.example.h` (tracked): safe example with labelled Serial,
  Network and Buffer sections and units; never real credentials.
- `include/config.h` (ignored by the root `.gitignore` pattern `**/config.h`):
  your local copy. Create it once with
  `cp -n include/config.example.h include/config.h` (from `software/firmware/`)
  and edit only the copy; never overwrite an existing `config.h`.

**Not consumed yet.** No source file includes either header. The simulator
`src/main.cpp` includes only `Arduino.h` and hardcodes its values, including
`Serial.begin(115200)`, so editing `config.h` changes nothing today. C++ uses a
header only through an explicit `#include`, and the ignore rule is independent
of that.

| Section | Current contents | Status |
| --- | --- | --- |
| Serial | `BARDBOX_SERIAL_BAUD 115200` (bit/s) | Matches the simulator's hardcoded value; not read by it |
| Network | Comments only: Wi-Fi on by default (agreed) | Not implemented; no settings defined |
| Buffer | Comments only: recording on by default (agreed) | Not implemented; no settings defined |

Wi-Fi SSID, password, endpoint and upload credentials are private: they go
only in the local `config.h` or runtime provisioning and have no defaults.
`platformio.ini` `monitor_speed` sets only the computer's serial monitor; it
must match the firmware baud but does not set `Serial.begin`.

Build values (pins, compiled drivers, partition layout, firmware version) are
separate from supported operational fields (UID, Wi-Fi, endpoint, upload
credential, intervals), which are intended to become runtime-provisioned
settings rather than compiled defines; not every field is compiled in. Secrets
are set-only and never printed, logged or committed. See the
[serial/base API proposal](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-base-and-serial-api.md).

## BardBoxSensorHealth.h

`SensorHealthTracker` records good samples and faults
(`noteGood`, `noteFault`), reports when a recovery attempt is recommended
(`recoveryDue`: enough consecutive faults and the cooldown elapsed), records the
attempt and its result (`noteRecoveryAttempt`, `noteRecoveryResult`), reports
staleness (`stale`, when `staleAfterMs` is set) and exposes a snapshot with
state (`Unknown`, `Healthy`, `Degraded`, `Recovering`, `Failed`) and counters.
The sensor driver owns ranges, units, rate-of-change rules, the actual
reinitialization and its safe state.

## BardBoxTransportRecovery.h

`TransportRecoveryTracker` records each delivery attempt once it is
acknowledged (HTTP Web Nodes: any `2xx`) or conclusively failed (`noteResult`),
reports when a retry is due (`retryDue`, after `retryBackoffMs`) and recommends
an escalating action (`nextAction`: `Reconnect`, `ReinitializeClient`,
`RestartDevice`) from consecutive failures. The node performs any reconnect,
client reset or restart; a restart hook is enabled only after testing on the
deployed hardware. It never authorizes discarding unacknowledged data.

Contracts: [Web Node protocol](https://github.com/bardphysicslab/bardbox/blob/main/docs/web-node-protocol.md) and the
[firmware README](../README.md#reliability-building-blocks). These headers are
unchanged by the [FW-LAYOUT-1](https://github.com/bardphysicslab/bardbox/issues/31#issuecomment-6290294824) scaffold.

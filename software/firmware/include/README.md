# include/

Standard PlatformIO project include folder: headers shared by this project's
`src/` files. It is not a BardBox module, and whether future helpers move into
`lib/` modules is not settled.

It holds four helpers and the tracked
[configuration example](#configuration-headers) `config.example.h`.
`BardBoxSensorHealth.h` and `BardBoxTransportRecovery.h` are policy and state
trackers only, and `BardBoxSensorValueRule.h` is a pure value check: none of
them performs hardware, sensor or network I/O. `BardBoxI2CRecovery.h` does
restart the I2C bus when called. The student `main.cpp` uses none of them.

## Configuration headers

- `include/config.example.h` (tracked): safe example with labelled Serial,
  Network and Buffer sections and units; never real credentials.
- `include/config.h` (ignored by the root `.gitignore` pattern `**/config.h`):
  your local copy. Create it once with
  `cp -n include/config.example.h include/config.h` (from `software/firmware/`)
  and edit only the copy; never overwrite an existing `config.h`.

**Read by the BardBox modules.** Each module in `lib/` includes `config.h`
when it exists, takes its own settings from its section, applies its
documented default for anything left out, and rejects invalid values at build
time. `main.cpp` does not include it. An older local copy without the newer
settings keeps working on the defaults.

| Section | Settings | Owner |
| --- | --- | --- |
| Serial | `BARDBOX_SERIAL_BAUD` (bit/s, default 115200) | `BardBoxSerial` |
| Identity | `BARDBOX_DEVICE_UID` (default `unassigned`) | `BardBoxInterface` |
| Network | `BARDBOX_NETWORK_ENABLED` (0/1, default 1); M1 reports it only | `BardBoxNetwork` |
| Buffer | `BARDBOX_RECORDING_INTERVAL_S` (s, default 60, 5..86400); `BARDBOX_BUFFER_CAPACITY_RECORDS` (default 256, 2..1024); `BARDBOX_BUFFER_RAM_RECORDS` (RAM retry slots, default 8) | `BardBoxBuffer` |

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

## BardBoxSensorValueRule.h

`SensorValueRule` and `validateSensorValue(value, rule, havePrevious,
previousValue, elapsedMs)` reject non-finite values and, when enabled by the
rule, values outside a min/max, a maximum change from the previous value, or a
maximum rate of change per second. It is standalone and optional: a sensor or
measurement definition supplies the rule, and it neither uses nor changes
`BardBoxSensorHealth.h`. It returns only valid or invalid, so the caller
chooses the fault code.

## BardBoxI2CRecovery.h

`recoverI2CBus(wire, config)` is an opt-in I2C transport recovery step above
`Wire.h`, called only after the project's health policy asks for recovery. On
ESP32 it calls `wire.end()` and then `wire.begin()` with no arguments. It does
not explicitly restore project settings, so preservation of custom SDA/SCL pins
or a `setClock()` frequency is **not guaranteed**: that depends on the Arduino
core, and a project using them must review its core's behavior or supply an
explicit restore hook. On other platforms it does nothing and returns `false`.
It has not been tested on hardware in this template.

Both headers moved here from the retired root `firmware/include/` folder, whose
older, unused `SensorHealthTracker` variant was removed in favour of
`BardBoxSensorHealth.h` above; the previous files remain in Git history.

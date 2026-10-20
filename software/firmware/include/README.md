# include/

Standard PlatformIO project include folder: headers shared by this project's
`src/` files. It is not a BardBox module, and whether future helpers move into
`lib/` modules is not settled.

It currently holds two helpers, and is the documented home for optional
[configuration headers](#configuration-headers). Both helpers are policy and state trackers only: they
perform no hardware, sensor or network I/O, and the current simulator
`main.cpp` does not use them.

## Configuration headers

Following the [repository layout](https://github.com/bardphysicslab/bardbox/blob/main/docs/repository-layout.md#firmware-configuration) rule, a firmware variant that needs
per-device build values uses:

- `include/config.example.h`: tracked, safe example values and documented
  defaults; never real credentials.
- `include/config.h`: ignored private local values (the root `.gitignore`
  pattern `**/config.h` already covers it).

The example header is documentation: it is not activated or compiled
automatically, and C++ uses a header only through an explicit `#include`. The
ignore rule is independent of either. The current simulator uses no
configuration, so neither file exists. Do not add one just to fill the tree.

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

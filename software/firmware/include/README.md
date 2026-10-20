# include/

Shared headers for this project. The two existing helpers are policy and state
trackers only: they perform no hardware, sensor or network I/O, and the current
simulator `main.cpp` does not use them.

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

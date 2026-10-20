# BardBoxNetwork

Proposed network delivery owner behind `BardBoxDevice`.

**Role:** Wi-Fi connect and bounded retry, oldest-first delivery of durable
records read from the Buffer, server acknowledgement, and requesting Buffer
removal only after that acknowledgement. Send-time diagnostics stay separate
from a record's stored metadata. Student code makes no network calls. Steps
must be bounded so serial service stays responsive; the scheduling model is
unresolved.

**Depends on:** `BardBoxBuffer`, settings (endpoint, credentials) and a
platform HTTP/Wi-Fi adapter. It may use
[`include/BardBoxTransportRecovery.h`](../../include/README.md) for recovery
policy.

Contracts: [Web Node protocol](https://github.com/bardphysicslab/bardbox/blob/main/docs/web-node-protocol.md) (published standard),
[Buffer API proposal](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-buffer-api.md).

Status: role README only ([FW-LAYOUT-1](https://github.com/bardphysicslab/bardbox/issues/31#issuecomment-6290294824), October 20, 2026). Nothing
here is implemented; there is no code in this folder. Names and interfaces are
proposals maintained in `bardbox/docs`, not accepted APIs.

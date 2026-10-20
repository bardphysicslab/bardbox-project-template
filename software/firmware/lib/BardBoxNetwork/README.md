# BardBoxNetwork

Proposed network delivery owner behind `BardBoxDevice`. Student code makes no network calls.

| Contract | Overview |
| --- | --- |
| Responsibility | Wi-Fi connect and bounded retry; oldest-first delivery of durable records; server acknowledgement |
| Inputs | Oldest durable record and its token from the Buffer; endpoint and credential settings; `nowMs` |
| Outputs | Pushed records; `acknowledge(token)` to the Buffer only after server acceptance; diagnostics |
| State | Connection and retry/backoff state; at most one in-flight record copy |
| Side effects | Network I/O. Send-time diagnostics stay separate from a record's stored metadata |
| Failures | Connection, timeout and server errors become retries and diagnostics; never treated as acknowledgement and never discard unacknowledged data |
| Dependencies | `BardBoxBuffer`, settings, platform Wi-Fi/HTTP adapter; may use [`include/BardBoxTransportRecovery.h`](../../include/README.md) |
| Compatibility | Published Web Node protocol. Each step must be bounded (deadline or work limit) so serial stays responsive; the scheduling model is unresolved |
| Status | Current scaffold: this README only, no code. Target: unimplemented proposal |

Authoritative contracts: [Web Node protocol](https://github.com/bardphysicslab/bardbox/blob/main/docs/web-node-protocol.md) (published), [Buffer API proposal](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-buffer-api.md).

Status: role README only, added by the [FW-LAYOUT-1](https://github.com/bardphysicslab/bardbox/issues/31#issuecomment-6290294824) scaffold (October
20, 2026). This folder contains no code; the target contract is not
implemented and its names are proposals maintained in `bardbox/docs`.

# BardBoxInterface

Beginner-facing facade: the only BardBox entry student code calls. The name
`BardBoxInterface` is accepted (October 20, 2026); its methods and results
are proposed.

| Contract | Overview |
| --- | --- |
| Responsibility | Field registration and `begin`; `poll(nowMs)` progress for the Serial, network and Buffer owners; latest-sample publication; completed-record handoff; shared generic checks; every-record metadata |
| Inputs | Declared fields with types and units; samples (`publishSample`); completed records the student built, raw or averaged, with window/method metadata (`submitRecord`); `nowMs` |
| Outputs | Per-channel results (`valid`, `missing`, `invalid`, `failed`); record admission (ticket or rejection) and `recordStatus`; latest snapshot to Serial; records to Buffer |
| State | Registered schema; latest accepted snapshot and its identity; outstanding record tickets (bounded) |
| Side effects | Stamps non-secret metadata (UID, MAC, firmware/protocol versions, time validity); never fabricated. No direct I/O of its own |
| Failures | Registration errors; structurally unusable sample or record rejected whole; coded per-channel nulls; record rejection or backpressure reported, never silently dropped |
| Dependencies | `BardBoxSerial`, `BardBoxNetwork`, `BardBoxBuffer`, settings and time modules; coordinates them without merging ownership |
| Compatibility | Existing `INFO`/`HEADER`/`READ` forms unchanged until a versioned contract is adopted |
| Status | Current scaffold: this README only, no code. Target: unimplemented proposal |

Authoritative contracts: [Serial/base API proposal](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-base-and-serial-api.md) (student contract draft), [decision index](https://github.com/bardphysicslab/bardbox/blob/main/docs/DECISIONS.md) (BB-005, BB-006).

Status: role README only, added by the [FW-LAYOUT-1](https://github.com/bardphysicslab/bardbox/issues/31#issuecomment-6290294824) scaffold (October
20, 2026). This folder contains no code; the target contract is not
implemented and its names are proposals maintained in `bardbox/docs`.

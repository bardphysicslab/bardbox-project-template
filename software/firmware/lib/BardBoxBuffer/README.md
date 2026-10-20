# BardBoxBuffer

Proposed durable record queue behind `BardBoxDevice`. Recording defaults on.

| Contract | Overview |
| --- | --- |
| Responsibility | Admit completed records, keep them durably and oldest-first until acknowledged |
| Inputs | Completed records from the facade; `acknowledge(token)` from the network owner |
| Outputs | Admission ticket or rejection; proposed states `pending`, `durable`, `uncertain`, `failed`; oldest record copy; status and loss counters |
| State | Stored records, queue metadata, saturation anchor, bounded pending operations |
| Side effects | Storage writes through a platform adapter |
| Failures | Rejection before any write (full, invalid size, exhausted sequence); `uncertain` when a write or metadata step fails after bytes may be written; at saturation the oldest unprotected record is evicted and counted as data loss, never as acknowledgement |
| Dependencies | Platform storage adapter; CESH `appendReadingToBuffer` is the modeling reference, not shared code |
| Compatibility | Storage format versioned; old records stay readable across firmware updates. Physical power-loss durability is unverified |
| Status | Current scaffold: this README only, no code. Target: unimplemented proposal |

Authoritative contracts: [Buffer API proposal](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-buffer-api.md), [compact record storage](https://github.com/bardphysicslab/bardbox/blob/main/docs/compact-record-storage.md) (draft).

Status: role README only, added by the [FW-LAYOUT-1](https://github.com/bardphysicslab/bardbox/issues/31#issuecomment-6290294824) scaffold (October
20, 2026). This folder contains no code; the target contract is not
implemented and its names are proposals maintained in `bardbox/docs`.

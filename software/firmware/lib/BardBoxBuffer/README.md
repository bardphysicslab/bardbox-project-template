# BardBoxBuffer

Proposed durable record queue behind `BardBoxDevice`. Recording defaults on.

**Role:** admit completed records, report pending/durable/failed outcomes
(an `uncertain` state is proposed), keep oldest-first order, remove a record
only after network acknowledgement, keep the oldest unacknowledged anchor and
count eviction as data loss at saturation. Never treats eviction or a failed
append as an acknowledgement.

**Depends on:** a platform storage adapter. CESH's `appendReadingToBuffer` is
the modeling reference, not shared code.

Contracts: [Buffer API proposal](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-buffer-api.md) and
[compact record storage](https://github.com/bardphysicslab/bardbox/blob/main/docs/compact-record-storage.md) (draft contract).

Status: role README only ([FW-LAYOUT-1](https://github.com/bardphysicslab/bardbox/issues/31#issuecomment-6290294824), October 20, 2026). Nothing
here is implemented; there is no code in this folder. Names and interfaces are
proposals maintained in `bardbox/docs`, not accepted APIs.

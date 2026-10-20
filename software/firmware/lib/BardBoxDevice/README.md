# BardBoxDevice

Proposed beginner-facing facade: the only BardBox entry student code calls.

**Role:** field registration and `begin`; `poll(nowMs)` giving the Serial,
network and Buffer owners bounded progress; latest-sample publication; the
completed-record handoff with ticket status; shared generic checks with
per-channel outcomes; every-record non-secret metadata (UID, MAC, firmware and
protocol versions, time validity), never fabricated.

**Depends on:** `BardBoxSerial`, `BardBoxNetwork`, `BardBoxBuffer`, settings
and time modules. It coordinates them without merging their ownership.

Contracts: [serial/base API proposal](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-base-and-serial-api.md)
(including the student contract draft), [decision index](https://github.com/bardphysicslab/bardbox/blob/main/docs/DECISIONS.md)
(BB-005, BB-006) and [architecture map](https://github.com/bardphysicslab/bardbox/blob/main/docs/architecture-map.md#node-firmware-template).

Status: role README only ([FW-LAYOUT-1](https://github.com/bardphysicslab/bardbox/issues/31#issuecomment-6290294824), October 20, 2026). Nothing
here is implemented; there is no code in this folder. Names and interfaces are
proposals maintained in `bardbox/docs`, not accepted APIs.

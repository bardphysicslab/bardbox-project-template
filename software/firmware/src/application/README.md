# src/application/

Optional home for student application code once a project outgrows
[`main.cpp`](../main.cpp), the start file. A small project can keep all of this
in `main.cpp`; nothing has to move here.

**Owns, wherever it lives:** sensor setup and reads (optionally through [`sensors/`](sensors/README.md)), the
acquisition schedule, sensor-specific readiness and fault detection, and any
averaging that turns samples into a completed record with its window/method
metadata.

**Uses:** only the `BardBoxDevice` facade (`lib/BardBoxDevice`): publish the
latest sample, submit completed records, read per-channel and record results.

**Does not:** write serial responses, call Wi-Fi/upload/retry/ACK, service the
Buffer, build device metadata (UID, MAC, versions, time validity) or repeat the
facade's generic checks (finite, type, required, bounds).

Contracts: [serial/base API proposal](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-base-and-serial-api.md)
(beginner API and the student contract draft) and the
[architecture map](https://github.com/bardphysicslab/bardbox/blob/main/docs/architecture-map.md#node-firmware-template).

Status: role README only ([FW-LAYOUT-1](https://github.com/bardphysicslab/bardbox/issues/31#issuecomment-6290294824), October 20, 2026). Nothing
here is implemented; there is no code in this folder. Names and interfaces are
proposals maintained in `bardbox/docs`, not accepted APIs.

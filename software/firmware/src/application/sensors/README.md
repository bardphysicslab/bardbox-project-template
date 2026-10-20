# src/application/sensors/

Optional home for sensor adapters or custom drivers, one per physical sensor,
when a project wants them out of `main.cpp`. Simple reads can stay in
[`main.cpp`](../../main.cpp).

**Owns:** initialization, board pins/addresses, reads, conversion readiness,
calibration, sensor-specific validity and fault codes, and driver recovery
(reinitializing the bus or device). [`include/BardBoxSensorHealth.h`](../../../include/README.md)
can track the fault/recovery state; the adapter performs the recovery itself.

**Returns:** typed values in declared units plus explicit failure, never zero
or a clamped value in place of a failed reading.

**Does not:** talk to serial, the network or the Buffer.

Contract: [serial/base API proposal](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-base-and-serial-api.md).

Status: role README only ([FW-LAYOUT-1](https://github.com/bardphysicslab/bardbox/issues/31#issuecomment-6290294824), October 20, 2026). Nothing
here is implemented; there is no code in this folder. Names and interfaces are
proposals maintained in `bardbox/docs`, not accepted APIs.

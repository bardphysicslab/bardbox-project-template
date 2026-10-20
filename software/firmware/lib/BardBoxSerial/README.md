# BardBoxSerial

Proposed serial owner behind `BardBoxDevice`.

**Role:** bounded command parsing and framing for the existing
`INFO`/`HEADER`/`READ`/`PING` forms and optional streaming, served from the
latest published snapshot. It never drains or acknowledges recorded records;
serial historical recovery is an open contract.

**Depends on:** an injected USB CDC/UART transport and the facade's latest
snapshot. No network or Buffer calls.

Contracts: [device instructions](https://github.com/bardphysicslab/bardbox/blob/main/docs/device-instructions.md) (published
standard) and [serial/base API proposal](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-base-and-serial-api.md).

Status: role README only ([FW-LAYOUT-1](https://github.com/bardphysicslab/bardbox/issues/31#issuecomment-6290294824), October 20, 2026). Nothing
here is implemented; there is no code in this folder. Names and interfaces are
proposals maintained in `bardbox/docs`, not accepted APIs.

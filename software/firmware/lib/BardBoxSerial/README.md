# BardBoxSerial

Proposed serial owner behind `BardBoxDevice`.

| Contract | Overview |
| --- | --- |
| Responsibility | Bounded command parsing and response framing; optional streaming |
| Inputs | Bytes from an injected USB CDC/UART transport; the facade's latest snapshot, schema and metadata |
| Outputs | `INFO`, `HEADER`, `READ` (`DAT` or `ERR`), `PING`/`PONG` and adopted extensions |
| State | Bounded input line; streaming on/off |
| Side effects | Writes to the serial transport only. Never drains or acknowledges recorded records |
| Failures | `ERR` results (for example `SENSOR_FAIL`, unknown command); oversized lines discarded through the delimiter |
| Dependencies | Transport and facade only; no network or Buffer calls |
| Compatibility | Published device-instruction forms stay unchanged; any per-channel or metadata serial form needs a versioned contract. Serial historical recovery is an open contract |
| Status | Current scaffold: this README only, no code. Target: unimplemented proposal |

Authoritative contracts: [Device instructions](https://github.com/bardphysicslab/bardbox/blob/main/docs/device-instructions.md) (published), [serial/base API proposal](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-base-and-serial-api.md).

Status: role README only, added by the [FW-LAYOUT-1](https://github.com/bardphysicslab/bardbox/issues/31#issuecomment-6290294824) scaffold (October
20, 2026). This folder contains no code; the target contract is not
implemented and its names are proposals maintained in `bardbox/docs`.

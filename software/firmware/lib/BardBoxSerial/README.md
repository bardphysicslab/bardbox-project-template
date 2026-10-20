# BardBoxSerial

Serial command handling and every serial wire form.

| Contract | Overview |
| --- | --- |
| Responsibility | Bounded line input (64 bytes; longer lines discarded through the newline), case-insensitive command recognition, `PING`/`PONG`, `ERR UNKNOWN_CMD`, and formatting of `INFO`, `HEADER`, `READ` and `READ_LIVE` |
| Interface | `begin(Stream&, SerialDataSource&)`, `beginPort(port, source)` (starts the port at the configured baud), `service(maxCommands)`: one bounded step |
| Output | Each reply is composed in a bounded 768-byte buffer and written only as far as `availableForWrite()` allows; partial writes resume next step, and no new command is read while a reply is pending, so a host that stops draining cannot stall recording. An oversized reply becomes `ERR REPLY_TOO_LONG`, never a cut line |
| Inputs | Bytes from the port; content through `SerialDataSource` (status, sensor and channel names, latest record, live cache snapshot) |
| Wire | `INFO` keeps `OK INFO uid= fw= sensors=` and adds fields. `HEADER`/`READ`/`READ_LIVE` use a **v2 candidate** framing pending review: `HDR,v2,record,boot,captured_ms,<sensor.measurement>...`; `DAT,v2,<record>,<boot>,<captured_ms>,<value or !code>...`; `LIVE,v2,<boot>,<now_ms>\|<sensor>,publish=<n or none>,handoff_age_ms=<n>,acquired=unknown,<measurement>=<value or !code>...`. Legacy `v1` parsers see `v2` and must not reinterpret it |
| Configuration | `BARDBOX_SERIAL_BAUD` (bit/s, default 115200, validated 1200..2000000 at build time) |
| Side effects | Writes to the serial port only; never drains or acknowledges records |
| Dependencies | Arduino `Stream`; the record types in `BardBoxBuffer`. No network or storage calls |

Shared contract: [serial/base API](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-base-and-serial-api.md).

Status: **progress** ([#36](https://github.com/bardphysicslab/bardbox-project-template/issues/36)): implemented and host-tested with simulated values; built for the Feather ESP32-S3 but not run on hardware. Names, wire forms and behaviors marked candidate are pending review.

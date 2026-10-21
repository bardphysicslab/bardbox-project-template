# BardBoxSerial

Serial command handling and every serial wire form.

| Contract | Overview |
| --- | --- |
| Responsibility | Bounded line input (160 bytes; longer lines discarded through the newline), case-insensitive command recognition, `PING`/`PONG`, `ERR UNKNOWN_CMD`, formatting of `INFO`, `HEADER`, `READ` and `READ_LIVE`, and the `CONFIG_*` settings commands (parsing and replies; validation and saving belong to `BardBoxSettings`) |
| Interface | `begin(Stream&, SerialDataSource&)`, `beginPort(port, source)` (starts the port at the configured baud), `useSettings(BardBoxSettings&)` (enables `CONFIG_*`; without it they answer `ERR UNKNOWN_CMD`), `service(maxCommands)`: one bounded step |
| Output | Each reply is composed in a bounded 1280-byte buffer (the worst-case `INFO`, about 1130 bytes, fits; host test) and written only as far as `availableForWrite()` allows; partial writes resume next step, and no new command is read while a reply is pending, so a host that stops draining cannot stall recording. An oversized reply becomes `ERR REPLY_TOO_LONG`, never a cut line |
| Inputs | Bytes from the port; content through `SerialDataSource` (status, sensor and channel names, latest record, live cache snapshot) |
| Wire | `INFO` keeps `OK INFO uid= fw= sensors=` and adds fields; it ends with the Wi-Fi diagnostics `wifi= ip= mac= rssi_dbm= wifi_ssid=<hex> receiver=` (`-` = unavailable; same names and meanings as the delivery body's `device_status`, see `lib/BardBoxNetwork/README.md`), then `net_error=` (the most recent connection failure since start, historical, `-` none; values and meaning in `lib/BardBoxNetwork/README.md`; also in `device_status`). `HEADER`/`READ`/`READ_LIVE` use a **v2 candidate** framing pending review: `HDR,v2,record,boot,captured_ms,<sensor.measurement>...`; `DAT,v2,<record>,<boot>,<captured_ms>,<value or !code>...`; `LIVE,v2,<boot>,<now_ms>\|<sensor>,publish=<n or none>,handoff_age_ms=<n>,acquired=unknown,<measurement>=<value or !code>...`. Legacy `v1` parsers see `v2` and must not reinterpret it |
| Configuration | `BARDBOX_SERIAL_BAUD` (bit/s, default 115200, validated 1200..2000000 at build time) |
| Side effects | Writes to the serial port only; never drains or acknowledges records |
| Dependencies | Arduino `Stream`; the record types in `BardBoxBuffer`; `BardBoxSettings` for `CONFIG_*`. No network calls; a `CONFIG_COMMIT` saves through `BardBoxSettings` on the service task |

## Device settings (`config/1`)

Candidate grammar pending review, for the per-device settings owned by
[`BardBoxSettings`](../BardBoxSettings/README.md). One reply line per command.
Values are hex-encoded bytes (`-` = empty), so any SSID byte survives the line
format. Values are never echoed; a password is reported only as `set` or
`empty`. Saved changes activate at the next start.

| Command | Reply |
| --- | --- |
| `CONFIG_SCHEMA` | `CONFIG_SCHEMA,version=1,encoding=hex,activation=next_boot,fields=uid:1:32:public;wifi_ssid:0:32:public;wifi_password:8:63:secret:empty_open` (`field:min:max:kind[:flag]`; `empty_open`: empty is also valid and means an open network) |
| `CONFIG_GET` | `CONFIG,version=1,store=<none\|saved\|invalid\|unavailable>,revision=<n>,uid=<uid>,wifi_ssid=<hex>,wifi_password=<set\|empty>,active_uid=<uid>,active_wifi_ssid=<hex>,active_wifi_password=<set\|empty>,pending=<0\|1>,activation=next_boot`: saved (next boot) values, then this boot's |
| `CONFIG_BEGIN <revision>` | `CONFIG_BEGIN_OK,revision=<n>`, `ERR,CONFIG_REVISION,revision=<current>` (stale read), `ERR,CONFIG_UNAVAILABLE` (no store) |
| `CONFIG_SET <field> <hex>` | `CONFIG_SET_OK,field=<field>`, `ERR,CONFIG_VALUE,field=<field>`, `ERR,CONFIG_FIELD` (unknown field); a refusal ends the update |
| `CONFIG_COMMIT` | `CONFIG_COMMIT_OK,revision=<n>,activation=next_boot`, `ERR,CONFIG_VALUE,field=<field>` (whole-record rule), `ERR,CONFIG_STORE,revision=<n>` (previous record kept; `<n>` is what the store holds) |
| `CONFIG_ABORT` | `CONFIG_ABORT_OK` |

`ERR,CONFIG_NO_TRANSACTION` and `ERR,CONFIG_EXPIRED` (60 s idle) answer SET or
COMMIT outside an update; `ERR,CONFIG_COMMAND` answers a malformed line. A lost
COMMIT reply is resolved by `CONFIG_GET`: the revision shows whether it was
saved. These commands are additive: `INFO`, `HEADER`, `READ`, `READ_LIVE` and
the legacy CESH `PROVISION_*`, `WIFI_*`, `REPORT_INTERVAL_*` and `CATCHUP_*`
contracts are unchanged; this firmware answers those legacy commands with
`ERR UNKNOWN_CMD`.

Shared contract: [serial/base API](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-base-and-serial-api.md).

Status: **progress** ([#36](https://github.com/bardphysicslab/bardbox-project-template/issues/36)): implemented and host-tested with simulated values; built for the Feather ESP32-S3 but not run on hardware. Names, wire forms and behaviors marked candidate are pending review.

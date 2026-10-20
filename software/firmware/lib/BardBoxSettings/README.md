# BardBoxSettings

Saved per-device settings that change without a rebuild: UID, Wi-Fi SSID and
Wi-Fi password. Owner of their validation, persistence and the distinction
between saved and active values.

| Contract | Overview |
| --- | --- |
| Responsibility | Validates and saves one settings record; loads it at start; reports saved (next boot) and active (this boot) values |
| Defaults and overrides | Compiled values (`BARDBOX_DEVICE_UID`, `BARDBOX_WIFI_SSID`, `BARDBOX_WIFI_PASSWORD` in `include/config.h`; without them `unassigned` and the open `BardWireless` network) are defaults. A saved record overrides all three. Invalid compiled defaults are not used (`unassigned`, no Wi-Fi) |
| Activation | Next start. Active values never change while running; no automatic restart |
| Fields | `uid`: 1-32 of `[A-Za-z0-9_.-]` (DB1 syntax, limited by the 32-character record attribution); `wifi_ssid`: 0-32 bytes, no control characters, empty = no Wi-Fi; `wifi_password`: empty (open network) or 8-63 bytes, no control characters. An empty SSID needs an empty password. Naming conventions (`bb-<site>-<type>-<instance>`) and global uniqueness are the registry's, not checked here |
| Update | `beginUpdate(revision)` (must match the saved revision), `setField`, `commit`, `abort`. A refused field or value ends the update; idle updates expire after 60 s; fields not sent keep their saved or default value |
| Persistence | `SettingsStore` interface, one record replaced as a whole. ESP32: `NvsSettingsStore`, NVS namespace `bb-settings`, key `s1`, relying on NVS replacing a key atomically (not verified on hardware here). The NVS partition is outside the firmware write ranges, so an ordinary reflash keeps settings; erasing the chip removes them. Record: `BBS1`, revision (u32 LE), three u8-length-prefixed fields, CRC-32 (zlib, LE) |
| Failures | A failed save keeps the previous record; `commit` reports what the store holds after reading it back, not what `save()` returned. An unreadable, corrupt or invalid record is ignored in favour of the defaults (`store=invalid`) and is replaced by the next save. Without a store nothing can be saved (`store=unavailable`) |
| Secrets | The password is set-only: no getter for tools, only `set`/`empty` |
| Records | Changing the UID never relabels records: each record stores the UID active when it was made |
| Consumers | `BardBoxInterface` (UID for INFO and new records; SSID/password for the ESP32 Wi-Fi transport), `BardBoxSerial` (`CONFIG_*` commands) |
| Not here | Endpoint, CA certificate and upload token stay compiled project configuration. Recording interval and catch-up are not settings in this module. Not the BardBox DC1/DB1 immutable provisioning records, which this template does not implement |

Serial commands (capability `config/1`, candidate pending review): see
[`lib/BardBoxSerial/README.md`](../BardBoxSerial/README.md#device-settings-config1).

Shared contract: [serial/base API, runtime settings](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-base-and-serial-api.md#runtime-settings-and-new-wire-commands) (proposed `CONFIG_*` family; this is the first implementation of a subset).

Status: **progress** ([firmware-tools#50](https://github.com/bardphysicslab/firmware-tools/issues/50), [#36](https://github.com/bardphysicslab/bardbox-project-template/issues/36)): host-tested with a simulated store; built for the Feather ESP32-S3 but not run on hardware.

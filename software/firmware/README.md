# Firmware

Firmware development for BardBox projects uses VS Code with PlatformIO.

The Arduino framework is acceptable, but it should be used through PlatformIO
rather than the Arduino IDE. Keep deployment firmware in this structure:

```text
software/firmware/
  platformio.ini
  src/main.cpp
  include/
  lib/
```

Layout and integrated firmware milestones ([#36](https://github.com/bardphysicslab/bardbox-project-template/issues/36)):
the BardBox modules in `lib/` now contain code, host-tested with simulated
values and built for the supported board, but not run on hardware. This is
progress toward the first usable template, not its completion: recording
persists to LittleFS (host-tested with simulated storage and restarts only),
and Network includes an ESP32 Wi-Fi/HTTPS adapter (host-tested/stubbed and compile-tested only). Radio/TLS/server behavior is not hardware-verified; OTA is not implemented. Start in `src/main.cpp`.

```text
software/firmware/
  platformio.ini                     Feather ESP32-S3 (8 MiB, no PSRAM) environments
  boards/                            project two-slot upload recipe (no TinyUF2)
  partitions_bardbox_8mb_ota.csv     OTA-ready 8 MiB layout (slots reserved; OTA not implemented)
  src/main.cpp                       student start file: version, sensors, read/publish/delay
  src/SimulatedBme280.h              SIMULATED driver for the M1 demo
  include/config.example.h           tracked settings example (Serial, Identity, Network, Buffer)
  include/config.h                   your ignored local copy
  include/                           also four standalone helpers (see include/README.md)
  lib/BardBoxInterface/              student API; wires the owners together
  lib/BardBoxMeasurements/           registration, publish checks, latest-reading cache
  lib/BardBoxBuffer/                 recording interval, records, durable FIFO queue and ACK
  lib/BardBoxSerial/                 serial commands and wire forms
  lib/BardBoxNetwork/                Wi-Fi/HTTPS, delivery policy/mailbox and candidate JSON
  lib/BardBoxSettings/               saved UID and Wi-Fi settings (override config.h, next boot)
  lib/BardBoxTime/                   UTC over NTP (one owner), record-creation timestamps
  test/host/                         host tests and demo (simulated)
```

Build: `pio run -e feather_s3` fails until `FIRMWARE_VERSION` is set in
`src/main.cpp` ("Please set FIRMWARE_VERSION in main.cpp."); `pio run -e
feather_s3_m1_test` builds with an explicit test version. Serial commands:
`INFO`, `HEADER`, `READ` (latest completed record), `READ_LIVE` (latest cached
readings), `PING` and the `CONFIG_*` settings commands; `HEADER`, `READ` and
`READ_LIVE` use candidate framings pending review (`HDR`/`DAT,v3` with the
record's UTC timestamp, `LIVE,v2`; see
`lib/BardBoxSerial/README.md`).

UID, Wi-Fi SSID and password can be changed over USB serial without a rebuild
(the flasher's Update settings, or `CONFIG_*`). Saved values override the
compiled defaults from the next start and survive an ordinary reflash; see
`lib/BardBoxSettings/README.md`.

The example UID is `bb-prj-air-001`. New UIDs must follow
`bb-<site>-<type>-<instance>` with 3-letter lowercase site/type codes and a
3-digit instance. Legacy `bb-0001` style IDs are deprecated for new projects.

Firmware only reports readings. It does not decide whether a node is stale or
unavailable; the Raspberry Pi/backend tracks communication freshness and
normalizes stale or unavailable API values to `null`.

## Reliability building blocks

New firmware may reuse these sensor- and transport-agnostic headers:

- `include/BardBoxSensorHealth.h`: tracks good samples, consecutive faults,
  recovery attempts, recovery failure, and stale readings. Individual drivers
  define their own validity limits, rate-of-change rules, and recovery action.
- `include/BardBoxTransportRecovery.h`: tracks acknowledgements and returns
  a bounded recovery step: reconnect, reinitialize the network client, then
  optional device restart.
- `include/BardBoxSensorValueRule.h`: optional per-measurement value check
  (finite, min/max, maximum change and rate of change); the rule comes from the
  sensor or measurement definition.
- `include/BardBoxI2CRecovery.h`: opt-in I2C bus restart above `Wire.h`. It
  does not explicitly restore custom pins or clock, so their preservation is
  not guaranteed; review the core's behavior or supply a restore hook.

The health and transport-recovery trackers and the value check are
state/value helpers that perform no I/O; the I2C helper is the only one that
acts on hardware, and only when a project calls it. None of them is a complete
networking or sensor driver. A Web Node still must persist before upload, retry oldest-first, delete only after
a `2xx` acknowledgement, and keep sampling separate from uploads. A restart
hook is optional and must only be enabled after it has been tested on the
deployed hardware. Publish the resulting health state through additive
`INFO`/payload diagnostics.

## Local configuration

Settings live in one place: tracked `include/config.example.h` (safe example
with Serial, Identity, Network and Buffer sections) and your ignored local copy
`include/config.h`, created once with `cp -n include/config.example.h include/config.h`
and never overwritten; see [`include/README.md`](include/README.md#configuration-headers).
BardBox modules read the local `config.h`; a setting left out uses the
module's documented default. Rename an existing
private `secrets.h` locally without replacing its values. Never add credentials
to the tracked example.

Use `hardware/ecad` for KiCad and `hardware/mcad` for mechanical/enclosure files.
Keep separate firmware projects under `software/firmware/<project>`; a single
project may use this directory directly.

## Flash a local build and set UID/Wi-Fi

1. Build: `pio run -e <environment>`. The files are in `.pio/build/<environment>/`:
   `firmware.bin` (the application), `bootloader.bin` and `partitions.bin` (the
   board layout).
2. Flash with the development flasher in `bardphysicslab/firmware-tools`
   (`tools/dev-flasher`), or with `pio run -e <environment> -t upload`. For an
   ordinary update, choose `firmware.bin` only. The flasher writes it into the
   inactive application slot and keeps the running firmware as the previous image.
   Saved settings (NVS) and stored records (LittleFS) are not written. Add
   `bootloader.bin` and `partitions.bin` only for a blank board or a layout repair
   (first install). Note that PlatformIO's own upload always writes the first slot and
   resets the boot record instead.
3. Press RESET, then use **Update settings** (or the `CONFIG_*` commands) to set the
   UID and Wi-Fi. They apply at the next start.

The default Wi-Fi network is the open `BardWireless` network (no password;
maintainer decision in [#36](https://github.com/bardphysicslab/bardbox-project-template/issues/36#issuecomment-6304409557)).
A project `config.h` that sets `BARDBOX_WIFI_SSID`/`BARDBOX_WIFI_PASSWORD` uses
its own values, and settings saved over USB serial override both. Endpoint, CA
and upload token have no defaults.

**OTA-ready layout, not OTA:** `partitions_bardbox_8mb_ota.csv` keeps two
application slots, a boot record and separate settings and record partitions, so
updates can keep the previous image and the saved data. Network OTA, with signed
images, rollout, health confirmation and rollback, is a later milestone and is not
implemented by this layout.

## Synthetic Wi-Fi demos

[Counter with UTC record timestamps](examples/counter/README.md):
`pio run -e feather_s3_counter_test` compiles it.



[Publish 42 through the API](examples/wifi42/README.md) is an isolated test sketch
and optional HTTPS receiver. `pio run -e feather_s3_wifi42_test` compiles it;
no hardware operation is performed by that command. Runtime Wi-Fi credentials,
HTTPS endpoint and trusted CA must be supplied in the ignored local config.

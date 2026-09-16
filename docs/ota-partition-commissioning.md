# Candidate ESP32-S3 OTA layout

`software/firmware/partitions_8mb_dual.csv` is an opt-in layout for verified 8 MiB
ESP32-S3 boards. It provides two 2 MiB application slots while retaining NVS,
OTA metadata, measurement filesystem and core-dump offsets. It is not a universal
Bardbox layout and is not enabled by default.

| Region | Offset | Bytes |
| --- | --- | --- |
| NVS | 0x9000 | 20,480 |
| OTA metadata | 0xe000 | 8,192 |
| Application 0 | 0x10000 | 2,097,152 |
| Application 1 | 0x210000 | 2,097,152 |
| LittleFS | 0x410000 | 4,063,232 |
| Core dump | 0x7f0000 | 65,536 |

The `spiffs` label/subtype remains for compatibility with the existing ESP32
LittleFS mount configuration; it does not change the filesystem to SPIFFS.

## Commissioning gate

Before installing this partition table, read the actual board/flash identity,
installed partition table, running image length and bootloader settings. Export
queued records and preserve device configuration. A source-tree partition file
does not establish what is installed on a particular device.

Do not perform an erase-all, format, or filesystem upload as automatic preparation.
Preserving offsets avoids moving data, but does not make a flashing procedure
safe by itself. A running image larger than 2 MiB must not simply have its slot
shrunk. Verify the selected application and bootloader support the new layout and
rollback. Record the exact recovery build and USB flashing procedure used.

After deliberate USB commissioning on one accessible bench device, verify both
slot addresses, known-good boot, configuration and queued records, then test failed
startup and interrupted update rollback. The updater must derive the inactive
slot and capacity from the installed partition table, not these constants alone.

CESH includes `ci_ota_bme680` and `ci_ota_bme280` compile environments for this
candidate. Its default build layout remains unchanged. A successful build does
not establish that a device is OTA-ready. Do not assign OTA releases until the
installer, trust provisioning, compatible queue reader and bench checks exist.

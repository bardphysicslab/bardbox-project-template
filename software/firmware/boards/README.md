# Supported board upload recipe

`bardbox_feather_s3_8mb_dual.json` preserves the installed Adafruit Feather
ESP32-S3 no-PSRAM board's CPU, memory mode, pins, USB IDs and variant. It selects
our 8 MiB dual-slot partition CSV, caps an app at 2 MiB, and removes the stock
TinyUF2 auxiliary image. CESH uses the same no-TinyUF2 approach.

Reason: stock `adafruit_feather_esp32s3_nopsram` uploads TinyUF2 at `0x2d0000`,
inside our second app slot (`0x210000` through `0x410000`, exclusive). Compiling
successfully does not detect that upload collision. Project profile provenance:
PlatformIO espressif32 7.1.3 board definition; recheck this recipe when upgrading.

Resolved PlatformIO upload environment was checked without running upload:
bootloader `0x0000`, partition table `0x8000`, initial boot-selection data
`0xe000`, main firmware `0x10000`; no TinyUF2. No filesystem image is part of
normal application Upload. This is a tool-side plan check, not proof of board
identity, previous partition compatibility or physical flashing success.

Build with `pio run -e feather_s3_wifi42_test`. Inspect the resolved environment
with `pio run -e feather_s3_wifi42_test -t envdump` (does not upload). The
regression `tests/test_firmware_flash_layout.py` guards the project recipe and
inherited test profiles. Existing USB browser-flasher bundle compatibility and
OTA lifecycle remain separate checks. Do not run erase or uploadfs as routine
recovery. Issue: https://github.com/bardphysicslab/bardbox-project-template/issues/36

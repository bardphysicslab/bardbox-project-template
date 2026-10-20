# src/

Application source for this PlatformIO project.

**Current (M1).** `main.cpp` is the student start file: it sets
`FIRMWARE_VERSION`, declares one sensor and its measurements, starts the
**simulated** BME280 in `SimulatedBme280.h` and BardBox, then reads, publishes
and calls `delay(500)` in `loop()`. Replace `SimulatedBme280` with a real driver;
its initialization and reads stay application code. Serial responses, records
and service are handled by the BardBox modules in `lib/`; the student writes no
transport code.

## Configuration

Configuration headers live in [`include/`](../include/README.md#configuration-headers),
not here. BardBox modules read `include/config.h` themselves; `main.cpp` does not
include it.

Status: **progress** ([#36](https://github.com/bardphysicslab/bardbox-project-template/issues/36)): implemented and host-tested with simulated values; built for the Feather ESP32-S3 but not run on hardware. Names, wire forms and behaviors marked candidate are pending review.

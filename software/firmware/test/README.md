# test/

Host tests and a host demo for the BardBox modules. They use the system C++
compiler and a minimal Arduino stub in `host/stub/`; values are simulated and
none of this is board or sensor evidence.

From `software/firmware/`:

```bash
test/host/run_host_tests.sh   # module checks plus the FIRMWARE_VERSION build guard
test/host/run_host_demo.sh    # runs the unmodified src/main.cpp with simulated time
```

The host build never reads the private `include/config.h`, so module defaults
apply. `tests/test_firmware_host.py` runs the host tests under pytest when a
C++ compiler is available.

Status: **M1 progress** ([#36](https://github.com/bardphysicslab/bardbox-project-template/issues/36)): implemented and host-tested with simulated values; built for the Feather ESP32-S3 but not run on hardware. Names, wire forms and behaviors marked candidate are pending review.

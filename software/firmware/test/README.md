# test/

Host tests and a host demo for the BardBox modules. They use the system C++
compiler (C++11, matching the ESP32 toolchain), a minimal Arduino stub in
`host/stub/` and an in-memory, fault-injecting storage (`host/FakeStorage.h`).
Values, storage and restarts are simulated; none of this is board, flash or
sensor evidence.

From `software/firmware/`:

```bash
test/host/run_host_tests.sh   # module checks, durable Buffer checks, FW-M2 review reproductions, version guard, fake Network delivery
test/host/run_host_demo.sh    # the unmodified src/main.cpp, then a simulated restart demo
```

The host build never reads the private `include/config.h`, so module defaults
apply. `tests/test_firmware_host.py` runs the host tests under pytest when a
C++ compiler is available.

Status: **progress** ([#36](https://github.com/bardphysicslab/bardbox-project-template/issues/36)): implemented and host-tested with simulated values; built for the Feather ESP32-S3 but not run on hardware. Names, wire forms and behaviors marked candidate are pending review.

Network checks use `host/FakeTransport.h`: no socket, DNS, TLS or server is
contacted. They verify finite phase deadlines (including delayed wake-up),
non-2xx retention, exact-token acknowledgements, retry after failed removal,
original attribution, and FIFO catch-up while recording and Serial continue.
The emitted `bardbox-delivery-candidate` body is parsed as JSON, not accepted
as a production protocol. Host time is simulated; task latency is not measured.

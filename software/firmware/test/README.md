# test/

Proposed home for this project's host and integration tests (PlatformIO
`test/`), covering the facade, Serial, network and Buffer owners together with
a sensorless or simulated consumer. Per-library tests may live in
`lib/<Library>/` when needed.

No tests exist yet and none run in CI. Acceptance targets are listed in the
[serial/base API proposal](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-base-and-serial-api.md#acceptance-and-implementation-status)
and [Buffer API proposal](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-buffer-api.md).

Status: role README only ([FW-LAYOUT-1](https://github.com/bardphysicslab/bardbox/issues/31#issuecomment-6290294824), October 20, 2026). Nothing
here is implemented; there is no code in this folder. Names and interfaces are
proposals maintained in `bardbox/docs`, not accepted APIs.

# src/

Application source for this PlatformIO project.

**Current.** `main.cpp` is a 65-line simulator: it returns fixed example
readings and writes the existing `INFO`/`HEADER`/`READ`/`PING` responses
itself. It has no runtime settings, network delivery, Buffer or private
configuration header, and does not use the `include/` helpers.

**Target (proposed, not implemented).** `main.cpp` is the start file to open
in VS Code with PlatformIO. Student code keeps its sensor setup, reads and
`BardBoxInterface` calls (`lib/BardBoxInterface`) here. Serial responses, network
delivery and recording are implemented by the private BardBox core in `lib/`,
behind the facade; the student writes no transport code. A larger project can
split its own files within `src/` later, only when needed; no subfolder is
prescribed.

## Configuration

Following the [repository layout](https://github.com/bardphysicslab/bardbox/blob/main/docs/repository-layout.md) convention, only
when a firmware variant needs per-device build values:

- `src/config.example.h`: tracked safe placeholders and documented defaults.
- `src/config.h`: ignored (`**/config.h` in the root `.gitignore`); private
  local values. Never commit credentials.

This simulator needs neither, so neither file exists here. Do not add one just
to fill the tree.

Build settings (pins, compiled drivers, partition layout, firmware version) stay
build choices. Supported per-device operational fields (UID, Wi-Fi, endpoint,
upload credential, intervals) are intended to become runtime-provisioned
settings rather than `config.h` defines; see the
[serial/base API proposal](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-base-and-serial-api.md) and
[firmware blueprint](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-blueprint.md). Secrets are set-only and are
never printed, logged or committed.

Status: this README was added by the [FW-LAYOUT-1](https://github.com/bardphysicslab/bardbox/issues/31#issuecomment-6290294824) scaffold (October 20,
2026). The existing simulator `main.cpp` is unchanged and is the only code
here; the target above, including the `BardBoxInterface` facade and runtime
settings, is not implemented. Names and interfaces are proposals maintained in
`bardbox/docs`.

# src/

Application source for this PlatformIO project.

**Current.** `main.cpp` is a 65-line simulator: it returns fixed example
readings and writes the existing `INFO`/`HEADER`/`READ`/`PING` responses
itself. It has no runtime settings, network delivery, Buffer or private
configuration header, and does not use the `include/` helpers.

**Target (proposed, not implemented).** `main.cpp` is the start file to open
in VS Code with PlatformIO. A small project can keep its sensor setup, reads
and `BardBoxDevice` calls (`lib/BardBoxDevice`) directly in `main.cpp`. Serial
responses, network delivery and recording are implemented by the private
BardBox core in `lib/`, behind the facade; the student writes no transport
code. [`application/`](application/README.md) and
[`application/sensors/`](application/sensors/README.md) are optional homes for
when a project grows or needs a custom driver; moving sensor code out of
`main.cpp` is never required.

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

Status: role README only ([FW-LAYOUT-1](https://github.com/bardphysicslab/bardbox/issues/31#issuecomment-6290294824), October 20, 2026). Nothing
here is implemented; there is no code in this folder. Names and interfaces are
proposals maintained in `bardbox/docs`, not accepted APIs.

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

Configuration headers live in [`include/`](../include/README.md#configuration-headers),
not here. `main.cpp` includes `config.h` only if its variant uses
configuration; the current simulator includes none.

Status: this README was added by the [FW-LAYOUT-1](https://github.com/bardphysicslab/bardbox/issues/31#issuecomment-6290294824) scaffold (October 20,
2026). The existing simulator `main.cpp` is unchanged and is the only code
here; the target above, including the `BardBoxInterface` facade and runtime
settings, is not implemented. Names and interfaces are proposals maintained in
`bardbox/docs`.

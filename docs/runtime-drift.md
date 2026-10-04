# Optional runtime expectations

Use BardBox Tools with runtime-drift support to extend `bardbox audit` (including
its JSON/MCP report) with explicitly approved runtime expectations. This requires
the shared-tooling foundation and runtime-drift changes; older tools ignore them.
Canonical policy: Bardbox `docs/service-operations-standard.md`.

Example syntax only — replace these illustrative versions/hashes with reviewed
values from the intended installation; do not copy them into a live manifest:

```toml
[runtime]
python = "3.12.13"

[runtime.packages]
fastapi = "0.116.1"

[[runtime.files]]
name = "watchdog-unit"
path = "/etc/systemd/system/example-watchdog.service"
sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
```

Package versions are exact installed distribution metadata, not requirement
ranges. Relative file paths resolve from the project root; absolute paths allow
installed unit files, shared drivers and rclone binaries. Declare each relevant
unit/drop-in/script independently. Do not inventory credentials or measurement
files. A missing/inaccessible/special/oversized/changing file is unverified and
fails the check; output never contains file contents. Files are limited to 32 MiB.

Run the tool using the service's Python environment on the Pi. Local development
results do not describe the Pi. Hash matches do not prove systemd loaded a unit,
a timer is enabled, a service restarted with the current checkout, or a backup
succeeded. Verify those operational facts separately during approved maintenance.

No expectations are enabled by this template. Each deployment must select its
reviewed baseline. Old manifests retain existing audit results without runtime
verification. Reconciliation is manual and reviewable; audits do not upgrade,
restart, write files or send messages.

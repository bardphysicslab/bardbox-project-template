# Deployment

Typical flow:

1. Create a new repo from `bardbox-project-template`.
2. Edit `raspi/config/app_config.example.json`.
3. Set `poll_interval_ms` and `node_stale_after_s`.
4. Replace the example driver with deployment drivers.
5. Replace the PlatformIO firmware example with device firmware.
6. Install requirements on the Raspberry Pi.
7. Run with `uvicorn` or a systemd service.

Use `BARDBOX_APP_CONFIG` to point the app at deployment config outside version
control.

The backend is responsible for freshness detection. Stale or unavailable nodes
must return `null` data values and a clear status.

## BardBox Operations

After `git pull`, run `python3 scripts/sync_app_config.py` to preview missing
example fields. Run `python3 scripts/sync_app_config.py --write` only after
reviewing the output. The recursive merge preserves deployment-specific values,
secrets, and unknown local fields and writes the live file atomically.

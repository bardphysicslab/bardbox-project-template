# Deployment

Typical flow:

1. Create a new repo from `bardbox-project-template`.
2. Keep `raspi/config/app_config.example.json` sanitized; copy it to ignored
   `raspi/config/app_config.json` for deployment values.
3. Set `poll_interval_ms` and `node_stale_after_s`.
4. Replace the example driver with deployment drivers.
5. Replace the PlatformIO firmware example with device firmware.
6. Install requirements on the Raspberry Pi.
7. Adapt and install the systemd service and watchdog examples under `deploy/`.
8. Verify both `/health` and three-failure watchdog recovery.
9. Add the Data API and verified backup only when the deployment has a clean
   historical readings root.

Use `BARDBOX_APP_CONFIG` to point the app at deployment config outside version
control.

The backend is responsible for freshness detection. Stale or unavailable nodes
must return `null` data values and a clear status.

## BardBox Operations

After `git pull`, run `python3 scripts/sync_app_config.py` to preview missing
example fields. Run `python3 scripts/sync_app_config.py --write` only after
reviewing the output. The recursive merge preserves deployment-specific values,
secrets, and unknown local fields and writes the live file atomically.

## Required web-service availability

Adapt `deploy/bardbox-app.service` to the repository path, service user, and
port, but retain:

```ini
Restart=always
RestartSec=5
```

Install the matching watchdog service/timer and executable
`scripts/watchdog.sh`. It checks the loopback `/health` endpoint every minute
and restarts the application only after three consecutive failures. Test process
recovery and application-hang recovery independently before production use.

## Remote administration checklist

Keep established Ethernet, LAN, and Bard VPN networking. Tailscale is the
recommended additive Raspberry Pi administration path:

```bash
curl -fsSL https://tailscale.com/install.sh | sh
sudo tailscale up
```

Prefer MagicDNS hostnames. Put conveniences such as `ssh solar` or `ssh rkc` in
the administrator's local `~/.ssh/config`; never commit machine-specific SSH
hosts, addresses, users, aliases, or keys.

## Conditional historical-data components

For a clean `data/readings/` root, set a dedicated non-empty `data_api.token` in
ignored `app_config.json`. Authenticated generic clients may list
`GET /api/data/files` and retrieve `GET /api/data/files/{path}`. The interface
is read-only, fail-closed, path-confined, and intended for `bardbox-mcp`.

Do not enable it for a mixed alert/audit/configuration tree. If local history is
retained and pruned, follow `docs/backup-reference.md`: copy without remote
deletion, verify stable exact file versions, record them atomically, and delete
locally only after an exact manifest match and retention expiry.

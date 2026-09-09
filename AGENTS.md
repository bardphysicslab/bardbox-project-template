# Agent Instructions

## BardBox standards first

The canonical standards live in the separate `bardbox` repository. Do not create a project-local replacement for a BardBox-wide rule. For Web Nodes, read:

- `docs/web-node-protocol.md`
- `docs/transport-recovery-standard.md`
- `docs/gpt-instructions.md`

in the canonical `bardbox` repository before changing protocol, buffering, upload, recovery, node health, or dashboard availability behavior.

## Web Node reliability rule

For a network-pushing node, acquisition, immutable completed-record persistence, and upload are independent responsibilities. A failed network operation must not stop sampling or discard a record.

- Persist a completed record before upload whenever practical.
- Upload oldest unacknowledged records first and remove one only after the configured acknowledgement (HTTP/HTTPS: a 2xx response).
- Give association, DNS, TLS, request, response, and acknowledgement operations finite deadlines.
- Use bounded retry/backoff and a documented recovery ladder.
- Reinitialize only resources the deployed platform actually controls. Do not claim a peripheral power-cycle exists without hardware support.
- A software watchdog/restart is optional last-resort recovery, only after a tested failure budget; it must not cause a restart loop or erase queued records.
- Expose additive health diagnostics: buffer state, last failure class, consecutive failures, replay state, and reset reason/boot count where supported.
- Keep automatic recovery quiet in production. Do not use `BUFFER_CLEAR` as automated recovery.

Apply the same outcomes to other transports, but do not copy HTTP-specific implementation into serial-poll, TCP-pull, BLE, or MQTT nodes.

## Compatibility

Add health and diagnostic fields without breaking existing payloads, commands, dashboards, or stored records. Preserve protocol-version compatibility; firmware and protocol versions are separate. Test fault paths deterministically and update the firmware version for any deployed firmware behavior change.

# Agent Instructions

## Shared guidance

Before planning, proposing, reviewing, delegating or implementing changes,
read the shared engineering guidance in
`bardphysicslab/engineering-standards`. Your tool does not load it
automatically.

1. Resolve `main` once per task.
   - With a local clone, usually `../engineering-standards` beside this
     repository: run `git -C ../engineering-standards fetch origin main`,
     then `git -C ../engineering-standards rev-parse origin/main`. If the
     fetch fails, use the existing `origin/main` and say it may be stale.
   - Without a clone, run
     `gh api repos/bardphysicslab/engineering-standards/commits/main --jq .sha`.
2. Read these files at that SHA:
   - `README.md`
   - `development-workflow.md`
   - `agent-practice.md`
   - `checkouts-and-worktrees.md`
   - `agent-coordination.md`

   With the clone, use `git -C ../engineering-standards show <sha>:<file>`.
   Without it, use
   `gh api "repos/bardphysicslab/engineering-standards/contents/<file>?ref=<sha>" -H "Accept: application/vnd.github.raw"`.
3. Record `Shared guidance: bardphysicslab/engineering-standards@<sha>` once
   in the task's durable evidence. If a review or proposal produces no such
   artifact, state it once in your response. A task that already recorded
   `bardphysicslab/bardbox@<sha>` keeps that governing commit.

Copies (desktop files, chat project sources, memory) do not substitute for
the resolved SHA. If a required file cannot be read at the resolved SHA, do
read-only investigation only, and report it. Do not implement, commit, push,
deploy or end checkouts unless the maintainer explicitly says to proceed
without it.

This is a BardBox project: also read bardbox's root `AGENTS.md` and
`ARCHITECTURE.md` at one resolved `main` commit of `bardphysicslab/bardbox`,
and the detailed standards relevant to the task.

## BardBox standards first

The canonical standards live in the separate `bardbox` repository. Do not create a project-local replacement for a BardBox-wide rule. For Web Nodes, read:

- `docs/web-node-protocol.md`
- `docs/transport-recovery-standard.md`
- the root `AGENTS.md` and `ARCHITECTURE.md` (the former `docs/gpt-instructions.md` now redirects there)

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

Add health and diagnostic fields without breaking existing payloads, commands, dashboards, or stored records. Preserve protocol-version compatibility; firmware and protocol versions are separate. Test fault paths deterministically: the health headers take an explicit `nowMs`, so fault sequences can be driven by a host test or a recorded simulated run. There is no firmware test harness or firmware CI on `main`; record which evidence you used. Update the firmware version for any deployed firmware behavior change.

## Firmware sensor reliability

When adding or changing BardBox firmware:

1. Use the shared sensor-health/recovery layer for attached sensors.
   Note: `software/firmware/include/BardBoxSensorHealth.h` (built) and
   `firmware/include/BardBoxSensorHealth.h` (not built, different API) currently
   both exist. Do not extend either or include both until a maintainer chooses one.
2. Keep the common layer sensor-agnostic. Never hard-code a sensor-specific physical range into shared health logic.
3. Put measurement-specific validity rules with the sensor/measurement definition or project configuration. Rules may include units, min/max, max delta, max rate of change, checksum validation, internal fault flags, or other device-specific constraints.
4. Treat transport errors, invalid samples, stale data, and device fault indications as distinct fault classes.
5. Do not publish a sample as valid after its driver/validator rejects it.
6. Use transport recovery helpers only when the firmware uses that transport. I2C recovery is an opt-in layer above `Wire.h`; do not fork or replace Arduino transport libraries.
7. Recovery must be bounded and observable: count failures, apply cooldowns, report health/diagnostics, and avoid infinite reset loops.
8. Preserve backwards compatibility by default. Existing node configs must still compile, and existing UID, protocol, commands, payload/CSV fields, and deployed behavior must not change unless a separately approved migration requires it.
9. Prefer additive defaults: new configuration knobs must have defaults so older project configs remain valid.
10. A project may make validation stricter than the template, but it may not weaken the shared compatibility and safety requirements without explicit maintainer approval.

Canonical platform work is tracked in `bardphysicslab/bardbox` issue #13.

Use tracked `config.example.h` and ignored local `config.h` for firmware configuration. Preserve values when migrating legacy private `secrets.h`; never replace them with example defaults. The repository-layout standard for ECAD/MCAD and software locations (`docs/repository-layout.md`) is proposed in bardbox PR #23 and is not yet on bardbox `main`.

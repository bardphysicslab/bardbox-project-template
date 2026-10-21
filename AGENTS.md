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

## Reference-first capability workflow

Follow [TEMPLATE-PROVING-1](https://github.com/bardphysicslab/bardbox-project-template/issues/31#issuecomment-6281614801)
and the [October 21 clarification](https://github.com/bardphysicslab/bardbox-project-template/issues/31#issuecomment-6314220319),
within the [shared development workflow](https://github.com/bardphysicslab/engineering-standards/blob/main/development-workflow.md).

- This repository is the maintained reusable implementation home **after
  promotion**. Develop and prove new capabilities in `BardBox-Reference` first;
  do not begin them here merely because this is the template. Maintenance of an
  already-promoted module stays with its maintained owner.
- Before work, check the [server roadmap/current issue checkpoints](https://github.com/bardphysicslab/bardbox-project-template/issues/31),
  [Reference checkpoint](https://github.com/bardphysicslab/BardBox-Reference/issues/1),
  [implementation decision index](docs/DECISIONS.md), applicable contracts, code
  and verification evidence. Reuse existing work and preserve local changes.
- A reusable capability is not complete until its tracked promotion is resolved:
  name the owning issue, pinned proven Reference revision, reviewed interfaces,
  tests and independent configured-consumer evidence. Promote reusable code,
  tests, configuration/examples and build/run guidance; do not blindly copy an
  instance or treat unreviewed contract proposals as accepted.
- After promotion, Reference keeps example configuration/demonstrations and
  consumes the maintained implementation through an explicitly reviewed
  dependency/version boundary, without competing maintained copies. The exact
  dependency mechanism belongs to the scoped promotion contract.
- Shared contracts, platform architecture and Mermaid diagrams remain in
  `bardbox`; shared engineering guidance remains in `engineering-standards`.
  Link those owners and reconcile accepted changes rather than duplicate them.
  No new repository/container or immediate runtime migration is required by this
  workflow; implementation, review, merge and deployment remain separate.

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

Add health and diagnostic fields without breaking existing payloads, commands, dashboards, or stored records. Preserve protocol-version compatibility; firmware and protocol versions are separate. Test fault paths deterministically: the health headers take an explicit `nowMs`, so fault sequences can be driven by a host test or a recorded simulated run. Firmware tests and target evidence currently belong to Reference; this template does not contain firmware. Record the actual verification level. Update the firmware version for any deployed firmware behavior change.

## Current firmware home

Kornelis explicitly directed [removing the duplicate firmware from this template](https://github.com/bardphysicslab/bardbox-project-template/issues/36#issuecomment-6315599802).
`software/firmware/` and its firmware-only tests/demo receiver are absent here.
Current firmware development, testing and cleanup belong in
[BardBox-Reference](https://github.com/bardphysicslab/BardBox-Reference/tree/main/software/firmware); consult that repository's instructions/checkpoint
and [Reference #5](https://github.com/bardphysicslab/BardBox-Reference/issues/5).
Do not recreate or develop another firmware copy here. Later reusable firmware
promotion/packaging is tracked by [#36](https://github.com/bardphysicslab/bardbox-project-template/issues/36)
and needs a separately reviewed scope; this removal does not start it.

Shared firmware contracts, recovery, sensor-health and protocol requirements
remain in `bardbox`; use those maintained standards and Reference's module
interfaces rather than deleted local headers. Preserve UID/payload/settings
compatibility and existing private configuration when future promotion is scoped.
Private local firmware settings/build material were retained in a private recovery
store outside repositories, never in Git. The existing ignore rules protect them
when older branches are checked out; never substitute safe examples for secrets.

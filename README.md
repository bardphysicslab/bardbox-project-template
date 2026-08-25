# bardbox-project-template

`bardbox-project-template` is the BardBox reference implementation and GitHub
template repo. New monitor projects should be created from this repo.

The canonical standards live in the separate `bardbox` repo. This template
implements those standards in working code: FastAPI backend, example driver,
dark BardBox dashboard, PlatformIO firmware example, scripts, docs, and tests.

## Repo Roles

`bardbox` is the standards/specification repo.

`bardbox-project-template` is the reference implementation/template repo.

Workflow:

1. Protocol or UI rule changes are documented first in `bardbox`.
2. Then they are implemented in `bardbox-project-template`.
3. New monitor repos are created from `bardbox-project-template`.
4. Existing monitor repos like GoLab, RKC, Solar, and CESH Air should be updated from the template standard when practical.
5. Project-specific repos should not invent protocol behavior unless it is promoted back into `bardbox` and `bardbox-project-template`.

Goal: one documented standard, one reference implementation, many project instances.

## What This Template Provides

- FastAPI Raspberry Pi app
- normalized reading API
- node freshness handling with `ok`, `stale`, `error`, and `node_unavailable`
- current node UID examples using `bb-<site>-<type>-<instance>`
- example driver contract
- CESH Air/RKC-derived BardBox landing and detail layouts
- reusable cards, status badges, foldouts, table, chart container, and mobile CSS
- exact lightweight `/health` endpoint and two-layer systemd/watchdog examples
- optional fail-closed read-only historical Data API
- canonical safe configuration synchronizer
- conditional verified-backup guidance and Tailscale deployment checklist
- VS Code + PlatformIO firmware example
- tests for stale/unavailable behavior

## Quick Start

```bash
python3 -m venv software/app/venv
source software/app/venv/bin/activate
pip install -r requirements.txt
uvicorn software.app.main:app --reload
```

Open [http://127.0.0.1:8000](http://127.0.0.1:8000). The landing page links to
the reference detail dashboard at `/monitor`.

Run commands from the repo root. The `software/app/` folder is a Python package;
do not `cd software/app` and run `uvicorn main:app`.

## First Customizations

1. Edit `software/app/config/app_config.example.json`.
2. Replace or add drivers under `software/app/drivers/`.
3. Replace the PlatformIO firmware example under `software/firmware/`.
4. Adjust dashboard labels and metric choices while preserving BardBox status/null behavior.
5. Add project-specific docs under `docs/`.

New node UIDs must use `bb-<site>-<type>-<instance>`, for example
`bb-prj-air-001`. Legacy `bb-0001` style IDs remain supported for existing
deployments but are deprecated.

During migration, add `legacy_uids` to a driver config to accept old
device-reported IDs while normalizing API/dashboard output to the canonical UID.
Historical logs are not rewritten.

## Firmware

Firmware uses VS Code + PlatformIO, not the Arduino IDE. Arduino framework
libraries are acceptable through PlatformIO.

## Repository Layout

```text
project/
├── software/
│   ├── app/
│   └── firmware/
├── hardware/
│   ├── ecad/
│   └── mcad/
├── data/
├── deploy/
├── docs/
├── scripts/
├── tests/
└── README.md
```

Firmware-specific structure:

```text
software/firmware/
  platformio.ini
  src/main.cpp
  include/
  lib/
```

## Standards Reference

Use `bardbox` as the specification repo for protocol, reading format, driver
boundaries, UI standards, channel names, and design decisions.
Network-uploading firmware must follow the canonical
[BardBox Web Node Protocol](https://github.com/bardphysicslab/bardbox/blob/main/docs/web-node-protocol.md);
this template keeps only a concise implementation checklist in
`docs/device-instructions.md`.

## BardBox Operations

After every source update, check whether the ignored live configuration needs
migration. Check mode performs no writes and exits non-zero if additions are
pending:

```bash
python3 scripts/sync_app_config.py --check
```

An informational preview is also available as either
`python3 scripts/sync_app_config.py` or
`python3 scripts/sync_app_config.py --dry-run`.

Apply the reviewed merge with:

```bash
python3 scripts/sync_app_config.py --write
```

The canonical BardBox synchronizer recursively adds new example fields while
preserving deployment values, secrets, and unknown local fields.
It matches nodes by UID, preserves local-only nodes, and does not automatically
create example-only nodes. Apply mode first creates a timestamped backup and
then atomically replaces the live file.

A deployment is ready to restart only when the check reports:

```text
Added keys: none
Added node fields: none
```

The deployment sequence is: pull, check, review/apply additions, check again,
restart, verify `/health`, and smoke-test the application.

`software/app/config/app_config.example.json` is sanitized and version
controlled. Copy it to ignored `software/app/config/app_config.json` for local
or production deployment values. Never place real secrets in the example.

## Availability Standard

Every deployed FastAPI/Uvicorn service needs both layers in `deploy/`:

- `Restart=always` and `RestartSec=5` recover an exited process;
- the one-minute watchdog checks `GET /health` and restarts after three
  consecutive failures, recovering a live but unresponsive process.

Adapt the generic paths, user, port, and service name before installation. Keep
the watchdog generic; do not add sensor or alert logic.

## Conditional Data API

The canonical router is included but disabled by default. It is appropriate only
when the deployment owns a clean `data/readings/` archive. Configure a dedicated
token only in ignored `app_config.json`:

```json
"data_api": { "token": "<dedicated read-only token>" }
```

An empty token fails closed. Authenticated consumers such as `bardbox-mcp` use
`GET /api/data/files` and `GET /api/data/files/{path}`. The API exposes only
CSV/CSV.GZ files and performs no writes, administration, or analysis. Remove or
leave the router disabled when a project has no suitable readings root.

## Optional Backup

Projects that retain and later prune local history should adapt the proven CESH
Air verified-manifest lifecycle. See `docs/backup-reference.md`. Stateless
projects should not add backup machinery.

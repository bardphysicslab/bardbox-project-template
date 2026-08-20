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
python3 -m venv raspi/venv
source raspi/venv/bin/activate
pip install -r requirements.txt
uvicorn raspi.main:app --reload
```

Open [http://127.0.0.1:8000](http://127.0.0.1:8000). The landing page links to
the reference detail dashboard at `/monitor`.

Run commands from the repo root. The `raspi/` folder is a Python package; do
not `cd raspi` and run `uvicorn main:app`.

## First Customizations

1. Edit `raspi/config/app_config.example.json`.
2. Replace or add drivers under `raspi/drivers/`.
3. Replace the PlatformIO firmware example under `firmware/`.
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

Expected structure:

```text
firmware/
  platformio.ini
  src/main.cpp
  include/
  lib/
```

## Standards Reference

Use `bardbox` as the specification repo for protocol, reading format, driver
boundaries, UI standards, channel names, and design decisions.

## BardBox Operations

Preview configuration synchronization after pulling source updates:

```bash
python3 scripts/sync_app_config.py
```

Apply the reviewed merge with:

```bash
python3 scripts/sync_app_config.py --write
```

The canonical BardBox synchronizer recursively adds new example fields while
preserving deployment values, secrets, and unknown local fields.

`raspi/config/app_config.example.json` is sanitized and version controlled.
Copy it to ignored `raspi/config/app_config.json` for local or production
deployment values. Never place real secrets in the example.

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

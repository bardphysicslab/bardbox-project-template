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
- RKC-style dark BardBox dashboard
- VS Code + PlatformIO firmware example
- tests for stale/unavailable behavior

## Quick Start

```bash
python3 -m venv raspi/venv
source raspi/venv/bin/activate
pip install -r requirements.txt
uvicorn raspi.main:app --reload
```

Open [http://127.0.0.1:8000](http://127.0.0.1:8000).

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

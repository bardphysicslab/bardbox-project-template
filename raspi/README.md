# Raspberry Pi App

This is the reference BardBox Pi app layer.

Rules:

- keep hardware protocol parsing inside drivers
- keep deployment identity in config
- keep `main.py` as orchestration and API policy
- return normalized readings only
- track last successful communication and last valid reading per node
- never present cached values as live after timeout or communication failure

Local development:

```bash
python3 -m venv raspi/venv
source raspi/venv/bin/activate
pip install -r requirements.txt
uvicorn raspi.main:app --reload
```

Run from repo root, not inside `raspi/`.

The API returns `ok`, `stale`, `error`, or `node_unavailable`. For stale,
error, and unavailable readings, data values are `null` and dashboards render
them as `—`.

# Raspberry Pi App

This is the deployment-facing Pi app layer.

Rules for customization:

- keep hardware-specific protocol parsing inside drivers
- keep deployment identity in config
- keep `main.py` as the orchestrator
- return normalized readings only from drivers

The included app runs immediately with the example driver and example config.

Local development is expected from the repo root:

```bash
python3 -m venv raspi/venv
source raspi/venv/bin/activate
pip install -r requirements.txt
uvicorn raspi.main:app --reload
```

Do not `cd raspi` and run `uvicorn main:app --reload`. The `raspi/` folder is
the app package, and imports are expected to resolve from repo root.

The starter dashboard uses a simulated example driver. Displayed readings are
generated in software, and the clock/timestamps come from the host operating
system time by default.

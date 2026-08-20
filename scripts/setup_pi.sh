#!/usr/bin/env bash
set -euo pipefail

python3 -m venv raspi/venv
source raspi/venv/bin/activate
pip install -r requirements.txt
mkdir -p data

if [ ! -e raspi/config/app_config.json ]; then
  cp raspi/config/app_config.example.json raspi/config/app_config.json
fi

echo "Pi setup complete."
echo "Next steps:"
echo "  1. Edit ignored raspi/config/app_config.json with deployment values."
echo "  2. Run from repo root: uvicorn raspi.main:app --host 0.0.0.0 --port 8000"
echo "  3. Adapt deploy/bardbox-app.service and the watchdog units."
echo "  4. Verify /health and both process/watchdog recovery layers."

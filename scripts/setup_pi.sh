#!/usr/bin/env bash
set -euo pipefail

python3 -m venv software/app/venv
source software/app/venv/bin/activate
pip install -r requirements.txt
mkdir -p data

if [ ! -e software/app/config/app_config.json ]; then
  cp software/app/config/app_config.example.json software/app/config/app_config.json
fi

echo "Pi setup complete."
echo "Next steps:"
echo "  1. Edit ignored software/app/config/app_config.json with deployment values."
echo "  2. Run from repo root: uvicorn software.app.main:app --host 0.0.0.0 --port 8000"
echo "  3. Adapt deploy/bardbox-app.service and the watchdog units."
echo "  4. Verify /health and both process/watchdog recovery layers."

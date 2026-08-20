#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${1:-http://127.0.0.1:8000}"

echo "Checking ${BASE_URL}/health"
curl -fsS --max-time 10 "${BASE_URL}/health"
echo

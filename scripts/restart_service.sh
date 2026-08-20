#!/usr/bin/env bash
set -euo pipefail

SERVICE_NAME="${1:-bardbox-app.service}"

sudo systemctl restart "$SERVICE_NAME"
sudo systemctl status "$SERVICE_NAME" --no-pager

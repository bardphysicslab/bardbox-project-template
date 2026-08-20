#!/bin/bash
set -u

HEALTH_URL="${BARDBOX_HEALTH_URL:-http://127.0.0.1:8000/health}"
SERVICE_NAME="${BARDBOX_SERVICE_NAME:-bardbox-app.service}"
FAILURE_LIMIT="${BARDBOX_FAILURE_LIMIT:-3}"
FAILURE_FILE="${BARDBOX_FAILURE_FILE:-/var/lib/bardbox-watchdog/failures}"

if curl -fsS --max-time 10 "$HEALTH_URL" >/dev/null; then
    printf '0\n' > "$FAILURE_FILE"
    exit 0
fi

failures=0
if [ -r "$FAILURE_FILE" ]; then
    read -r recorded_failures < "$FAILURE_FILE" || true
    case "$recorded_failures" in
        ''|*[!0-9]*) failures=0 ;;
        *) failures="$recorded_failures" ;;
    esac
fi

failures=$((failures + 1))
printf '%s\n' "$failures" > "$FAILURE_FILE"
logger "BardBox watchdog: health check failed (${failures}/${FAILURE_LIMIT})"

if [ "$failures" -ge "$FAILURE_LIMIT" ]; then
    logger "BardBox watchdog: restarting ${SERVICE_NAME} after ${failures} failed health checks"
    if systemctl restart "$SERVICE_NAME"; then
        printf '0\n' > "$FAILURE_FILE"
    fi
fi

exit 0

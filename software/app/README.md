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
python3 -m venv software/app/venv
source software/app/venv/bin/activate
pip install -r requirements.txt
uvicorn software.app.main:app --reload
```

Run from repo root, not inside `software/app/`.

The API returns `ok`, `stale`, `error`, or `node_unavailable`. For stale,
error, and unavailable readings, data values are `null` and dashboards render
them as `—`.

## Local test receiver (development only)

A diagnostic receiver for posting test samples and inspecting what arrived. It
is off unless `BARDBOX_TEST_RECEIVER=1`; any value other than unset, `0` or `1`
stops startup. Code: `software/app/local_receiver.py`.

It accepts one **normalized diagnostic test sample** per request (`uid`,
`timestamp`, `data`, `extended.firmware_version`, `extended.protocol_version`,
optional `extended.record_id`). This is a test format, **not a firmware wire
protocol**; nothing here shows firmware or API conformance. The protocol version
is displayed as *unchecked*, and a configured UID is a claim, not
authentication. Results live only in memory (last 20). Nothing is written to
`data/readings`, shown in `/readings/latest` or acknowledged as archived.

Launch over HTTPS on loopback only, with a short-lived self-signed certificate
kept outside the repository:

```bash
CERT_DIR=$(mktemp -d) && chmod 700 "$CERT_DIR"
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 1 \
  -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" \
  -keyout "$CERT_DIR/key.pem" -out "$CERT_DIR/cert.pem"
BARDBOX_TEST_RECEIVER=1 .venv/bin/uvicorn software.app.main:app --host 127.0.0.1 --port 8443 \
  --ssl-keyfile "$CERT_DIR/key.pem" --ssl-certfile "$CERT_DIR/cert.pem" \
  --ssl-ciphers 'ECDHE+AESGCM:ECDHE+CHACHA20'
```

Post a sample (the `X-BardBox-Test: 1` header is required, so a real node
accidentally pointed here gets a non-2xx and keeps its data):

```bash
curl --cacert "$CERT_DIR/cert.pem" -H 'Content-Type: application/json' -H 'X-BardBox-Test: 1' \
  --data '{"uid":"bb-prj-air-001","timestamp":"2026-10-20T16:00:00Z","data":{"temp_c":21.6,"rh_pct":43.0,"press_pa":100980},"extended":{"firmware_version":"1.0.0","protocol_version":"test"}}' \
  https://127.0.0.1:8443/dev/test-receiver/samples
```

Then open `https://127.0.0.1:8443/dev/test-receiver` (HTML) or
`/dev/test-receiver/samples` (JSON).

Limitations: the self-signed certificate is not a valid CA-issued certificate,
so browsers warn and `curl` needs `--cacert`. Bard's web and hosting
requirements still apply as a direction; this local setup is a maintainer-scoped
development choice, not a Bard IT exemption, and needs an applicability and
security review before anything Bard-facing. Only this receiver sets the
security headers; the rest of the app does not yet. Requests from anything
other than `127.0.0.1`/`::1` are refused.

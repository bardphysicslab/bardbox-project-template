# Publish the answer: 42

This disposable synthetic node uses the same student API as the BME example:
register `demo.answer`, then `demo.publish(42.0f)` every half second. The Buffer
records at the configured recording interval, independently of publication and
Wi-Fi retry. The default is 60 seconds. It sends candidate JSON and never
claims a real sensor reading or production archive compatibility.

## What is verified

- Host demo: actual sketch -> API -> recorded value 42 -> simulated delivery.
- Adapter policy and native API configuration checked with fakes/stubs.
- ESP32-S3 `feather_s3_wifi42_test` compiles; not flashed or run on a board.
- The actual sketch-generated body is accepted by the isolated HTTPS receiver
  on loopback with certificate and hostname verification; wrong bearer and
  unrelated formats are rejected. That test used a disposable certificate and
  stopped its receiver afterward; it is not radio/device TLS evidence.

## Prepare the operator-run test

Keep real values only in ignored `include/config.h`; never overwrite an existing
local config. Set the Network section's `BARDBOX_WIFI_SSID`, Wi-Fi password if
needed, `BARDBOX_NETWORK_ENDPOINT`, `BARDBOX_NETWORK_CA_PEM`, and optional
`BARDBOX_NETWORK_BEARER_TOKEN`. For the supplied receiver the bearer token is
required and must match its private `BARDBOX_DEMO_TOKEN` environment value.
A certificate/CA must be valid for the exact endpoint hostname/IP the board uses.
Only HTTPS is supported; no insecure fallback or system trust-store change.
Clock synchronization uses the configured NTP server if the clock is unset.

From `software/firmware`, compile only:

```sh
pio run -e feather_s3_wifi42_test
```

The test environment supplies an explicit test firmware version. `src/main.cpp`
is unchanged; this environment selects `examples/wifi42/main.cpp` instead.
The private config is unchanged and omitted settings use module defaults.

For the receiver, use a test certificate and private key stored outside Git,
and set a private 16..256 character visible-ASCII `BARDBOX_DEMO_TOKEN` in the
receiver environment. From the repository root:

```sh
python3 scripts/wifi42_receiver.py --cert /path/to/test-cert.pem --key /path/to/test-key.pem
```

This defaults to loopback. For a physical board test, explicitly choose a
**private LAN** bind/address reachable from the device (`--bind`), and point the
board's HTTPS endpoint at `/demo/wifi42` on that address/port (default 8443).
Do not use this diagnostic receiver as a public/production service. The earlier
application normalized test receiver is loopback-only, has another format,
and is not modified by this demo.

Receiver acceptance prints `demo.answer = 42` and replies 204. It keeps **no
archive**, so this 204 accepts only disposable demo data; the firmware will
remove that test record from its delivery queue. No production delivery or
server deduplication guarantee. Stop the receiver with Ctrl-C. Hardware flashing
and board operation remain separate operator actions; no such action has been
performed. Actual Wi-Fi, native TLS/SNI, clock/DNS, heap/stack and recovery still
need a recorded on-board test before calling Wi-Fi ready.

## Board profile

All supported environments use the [project two-slot board recipe](../../boards/README.md),
which excludes the stock TinyUF2 write inside app1. A successful build alone
never established this upload compatibility. The resolved upload image plan is
checked separately; browser USB-flasher bundle compatibility is still pending.

### Viewing arrivals

Open the receiver's HTTPS address at `/demo/wifi42` in a browser to see the
latest accepted synthetic value, server arrival time (UTC), and accepted-upload
count. The page refreshes every three seconds. These are arrivals, not unique
measurements: retries and backlog catch-up can increase the count. The page is
visible to anyone able to reach this deliberately private demo listener; it
contains only synthetic 42 data, never credentials or arbitrary payload fields.
State is kept only in memory and resets on receiver restart; firmware keeps
its persistent queue. POST authentication and acknowledgement are unchanged.

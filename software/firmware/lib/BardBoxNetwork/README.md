# BardBoxNetwork

Owns delivery attempts, phase deadlines, retry/backoff and Network configuration.
The ESP32 implementation now supplies a Wi-Fi/verified-HTTPS adapter. Its portable
policy and native API calls are host-tested through fakes; S3 builds compile.
Actual radio, DNS and TLS operation on the board has not been verified.

| Contract | Overview |
| --- | --- |
| Input | Oldest persisted record copied by the Buffer owner through a one-slot request/result mailbox; explicit transport and timing settings |
| Output | Candidate JSON body, exact-token acknowledgement request, and additive INFO status/counters |
| State | Volatile attempt ID, current phase/deadline, capped backoff, counters; durable records remain owned by Buffer |
| Side effects | Calls only the injected transport and mailbox; never mutates Buffer from the Network task |
| Acceptance | Current-attempt HTTP 2xx from the configured endpoint, verified by the transport, before its deadline |
| Failure | Timeout, unverified/wrong-attempt response, malformed response, redirect or non-2xx retains the head and retries with capped backoff |
| Deletion failure | Record is delivered again with the same queue token; the receiver must handle duplicate receipt |
| Wi-Fi diagnostics (template#36) | Network owns a bounded `ConnectivityStatus` snapshot (`BardBoxConnectivity.h`), refreshed about once a second on its task from the transport's `linkStatus()` and published through the locked mailbox: `wifi` (`disabled`, `unconfigured`, `disconnected`, `connected`), station `ip`, station `mac`, `rssi_dbm`, effective `wifi_ssid` (saved setting or default) and `receiver` (endpoint as `scheme://host[:port]` only). IP and RSSI are cleared unless connected; nothing stale is shown as current. Serial `INFO` and the delivery body's `device_status` carry these same names and meanings. No password, bearer token, or URL path, query or user info. Diagnostics only: delivery and acceptance are unchanged |
| Delivery body `device_status` | Candidate: a separate object `{"at":"send","wifi","ip","mac","rssi_dbm","wifi_ssid","receiver","net_error"}`, with unavailable values as `null`, describing the sender at send time (each attempt). It is not part of the record, which keeps its original attribution and measurement-time fields |
| Connection failure detail | `connection_failed` keeps its meaning. `net_error`, in INFO and in `device_status` (shared snapshot), is **historical**: the most recent connection failure since start, not cleared by a later success (`-`/`null` none). Values, numbers only: `link` (Wi-Fi lost), `dns`, `tcp:0x<esp-tls error>`, `tls_setup:0x<esp-tls error>` (for example an unparsable CA PEM), `tls:-0x<mbedtls error>:0x<certificate verify flags>`, `connect[:0x<esp-tls error>]` (not classifiable), `send`, or `unknown`. The ESP32 provider classifies from the error esp-tls recorded (`esp_tls_get_and_clear_last_error`, read before close), never from the final connection state: in IDF 4.4.7 one call can pass from TCP connect into a failing handshake. Non-blocking IDF 4.4.7 does not check `SO_ERROR`, so a refused TCP connection appears as `tls:` with an mbedtls network error (`-0x004x`/`-0x005x`). Verify flags show certificate problems, for example `0x4` name mismatch, `0x8` not trusted, `0x1` expired, `0x200` not yet valid; the time gate only ensures the clock was set after 2024, so a wrong clock can still produce the last two. Never certificate contents, URL paths or secrets |
| Dependencies | Buffer record/token types and serialized owner-side adapter; concrete transport supplied by project composition |

`BardBoxDelivery` keeps Buffer access on its single service-task owner. Network
runs separately on ESP32. Mailbox locks cover copies, never storage or transport
calls. An injected transport must return promptly from start/step/abort; a task
cannot make an unbounded provider safe. Actual task/flash latency is unmeasured.

Settings default in `BardBoxNetworkConfig.h`; project overrides belong in the
Network section of ignored `include/config.h`. Enabled defaults to 1. Private
endpoint, Wi-Fi credentials and upload credentials have no defaults. Missing
endpoint reports `unconfigured`; configured without an injected host adapter reports
`no_transport`; disabled reports `disabled`. On ESP32 the default adapter also
requires SSID and a trusted CA; absent/invalid settings report `unconfigured`.
Credentials are used only by the real adapter, never returned through INFO. Phase deadlines and retry/idle intervals have compile-time
bounds. Recording cadence remains owned by Buffer, separate from retry timing.

`BardBoxPayload` emits `bardbox-delivery-candidate` JSON only for this isolated
exercise. It preserves the saved UID, firmware/protocol and channel schema,
represents missing values as null, and labels acquisition time unknown. It is
**not** the accepted firmware wire format or the existing receiver's normalized
test format. No receiver compatibility or end-to-end archive delivery is claimed.

Run `sh test/host/run_host_tests.sh` from `software/firmware` for simulated
phase failures, late-response rejection, retained/retried head, failed deletion,
original attribution, and FIFO catch-up while recording and Serial continue.
Host tests are sequential controlled simulations, not RTOS race or physical
network evidence. The sample body is also parsed as JSON.

Shared contract: [serial/base API](https://github.com/bardphysicslab/bardbox/blob/main/docs/firmware-base-and-serial-api.md)
and [Web Node protocol](https://github.com/bardphysicslab/bardbox/blob/main/docs/web-node-protocol.md).
Tracked by [template issue 36](https://github.com/bardphysicslab/bardbox-project-template/issues/36).
The six API proposals remain unaccepted. Network/OTA hardware readiness remains
unfinished; nothing in this milestone is deployment or flashing evidence.

## ESP32 Wi-Fi/HTTPS adapter

`BardBoxWifiTransport` owns association cooldown (30 s default), bounded HTTP
request/response parsing and the transport phases. `BardBoxEsp32Wifi` implements
the I/O using station mode, asynchronous LwIP DNS on the TCP/IP thread, and
ESP-TLS asynchronous connect/read/write. Interface selects it automatically;
student `begin()` and `publish()` calls stay unchanged. Connection maintenance
starts before the first record, without requiring an empty test upload.

Project Network configuration: `BARDBOX_WIFI_SSID`, optional
`BARDBOX_WIFI_PASSWORD` (omitted/empty means open Wi-Fi),
`BARDBOX_NETWORK_ENDPOINT` (HTTPS only), `BARDBOX_NETWORK_CA_PEM`, optional
`BARDBOX_NETWORK_BEARER_TOKEN`, `BARDBOX_WIFI_RECONNECT_MS`, optional
`BARDBOX_NETWORK_NTP_SERVER` (CESH default `pool.ntp.org`). No credentials or
endpoint are supplied by default. Settings strings are compiled, immutable
and device-lifetime; runtime provisioning remains later work.

CESH contributes the station/reconnect behavior, CA verification, no redirect
following, and 2xx delivery policy. Its legacy insecure TLS fallback and blocking
HTTP calls were not copied. Peer chain and original endpoint hostname/SNI remain
verified even though the connection uses a separately resolved IP. A valid clock
is required; background SNTP starts if the clock is unset, and association-phase
timeout/backoff applies while waiting. No automatic device reboot or Wi-Fi NVS
credential persistence. Headers/credentials and server bodies are never logged.

The provider has device-lifetime callback storage. After DNS timeout, a new
attempt is refused until the original callback completes, preventing callback
reuse/UAF or unbounded concurrent resolvers. TLS/socket cleanup stays on the
Network task. Request writes are at most 512 bytes/step; reads at most 128;
headers are capped at 8 KiB total and 511 bytes per line. Only a fully received,
well-framed HTTP status/header block can become an outcome. Invalid/overflowing
Content-Length, conflicting lengths, ambiguous Transfer-Encoding/length and
unsupported transfer codings are rejected. Redirects are returned
unchanged and never followed. Informational responses fail this minimal attempt
rather than being treated as acceptance. Response bodies are not read or stored.
IPv6 literal URLs, URL credentials, fragments, header injection, overlong URLs
and invalid ports are rejected. This is a deliberately limited HTTP/1.x client.

Installed ESP-IDF details matter: its asynchronous TCP connection uses a NULL
`select` timeout when configured with zero. The provider uses **1 ms**, and
rearms consumed descriptor sets between polls. Original hostname verification
uses `common_name` with skipping disabled. Sources checked:
[ESP-TLS connection implementation](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp-tls/esp_tls.c)
and [TLS hostname setup](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp-tls/esp_tls_mbedtls.c).
These are installed-IDF-specific integration assumptions; recheck when updating
the platform package. CA parsing/handshake CPU cost, task stack/heap headroom and
radio scheduling still require device measurements; 1 ms is a socket polling
limit, not a proven maximum for an entire TLS service step.

The candidate body is for the isolated 42 test, not existing production CESH
Ingestion. See [wifi42 demo](../../examples/wifi42/README.md). The previous
loopback-only normalized test receiver is unchanged and accepts a different
format. No production receiver adoption is implied.

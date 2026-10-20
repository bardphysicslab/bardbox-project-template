# BardBoxNetwork

Owns delivery attempts, phase deadlines, retry/backoff and Network configuration.
This milestone uses an injected **simulated transport**; it does not implement a
Wi-Fi or HTTPS adapter and does not contact an endpoint.

| Contract | Overview |
| --- | --- |
| Input | Oldest persisted record copied by the Buffer owner through a one-slot request/result mailbox; explicit transport and timing settings |
| Output | Candidate JSON body, exact-token acknowledgement request, and additive INFO status/counters |
| State | Volatile attempt ID, current phase/deadline, capped backoff, counters; durable records remain owned by Buffer |
| Side effects | Calls only the injected transport and mailbox; never mutates Buffer from the Network task |
| Acceptance | Current-attempt HTTP 2xx from the configured endpoint, verified by the transport, before its deadline |
| Failure | Timeout, unverified/wrong-attempt response, malformed response, redirect or non-2xx retains the head and retries with capped backoff |
| Deletion failure | Record is delivered again with the same queue token; the receiver must handle duplicate receipt |
| Dependencies | Buffer record/token types and serialized owner-side adapter; concrete transport supplied by project composition |

`BardBoxDelivery` keeps Buffer access on its single service-task owner. Network
runs separately on ESP32. Mailbox locks cover copies, never storage or transport
calls. An injected transport must return promptly from start/step/abort; a task
cannot make an unbounded provider safe. Actual task/flash latency is unmeasured.

Settings default in `BardBoxNetworkConfig.h`; project overrides belong in the
Network section of ignored `include/config.h`. Enabled defaults to 1. Private
endpoint, Wi-Fi credentials and upload credentials have no defaults. Missing
endpoint reports `unconfigured`; configured without an adapter reports
`no_transport`; disabled reports `disabled`. No credentials are read or sent by
the fake transport. Phase deadlines and retry/idle intervals have compile-time
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

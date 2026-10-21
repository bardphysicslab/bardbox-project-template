# BardBoxTime

The one owner of UTC on the device: network time over NTP, record-creation
timestamps, and the clock gate for HTTPS certificate checks. Owning tracker:
[#49](https://github.com/bardphysicslab/bardbox-project-template/issues/49).

| Contract | Overview |
| --- | --- |
| Owns | UTC availability, its source and validity; the single SNTP client (Network's TLS clock gate asks Time and no longer starts its own) |
| Inputs | Wi-Fi connectivity (from Network's status snapshot); an injected `TimeSyncProvider` (ESP32: lwIP SNTP via `configTime`, UTC; hosts: a fake) |
| Outputs | `utcNow()` for record creation and the TLS gate; a bounded `TimeStatus` (`time=unavailable\|unsynced\|synced`, `time_sync_age_s`, `record_utc=on\|off`, `utc` = current clock or unknown) for Serial INFO and the delivery body's `device_status`, with the same names and meanings |
| Synchronization | Starts once, the first time Wi-Fi is connected; never blocks recording, Serial or delivery. Refreshes every `BARDBOX_TIME_REFRESH_S` (default 3600 s). Server `BARDBOX_TIME_NTP_SERVER` (default `pool.ntp.org`; the earlier `BARDBOX_NETWORK_NTP_SERVER` is still honoured) |
| Validity | UTC is valid only after a successful synchronization **this boot** (no RTC yet), not merely because the clock reads a plausible date. Before that, UTC is unknown and never invented. Monotonic time since boot stays the basis for recording schedules and elapsed-time diagnostics; UTC corrections never move recording boundaries |
| Record timestamps | Each record gets `recorded_utc`: the UTC time it was created at its recording boundary (not acquisition time, which stays `unknown`, and not upload time). Stored with the record (payload schema 3) and never relabelled across retries, restarts or later synchronizations. Serial `READ` shows the same saved value and status (`DAT,v3`). `recorded_utc_status`: `ntp`, `unsynced` (enabled, clock not yet synchronized), `disabled` (option off), or `not_recorded` (queued by earlier firmware; schema 2 still decodes) |
| Option | `BARDBOX_TIME_RECORD_TIMESTAMPS` (1 default, 0 off). Off affects only record creation: Time, NTP, the system clock and HTTPS certificate validation keep running, and records keep their ID, boot and monotonic order. It applies when a record is created; saved records keep what they were created with |
| Student code | No timestamp calls: `publish()` is unchanged and records are stamped automatically |
| Failure | No Wi-Fi or NTP: `time=unsynced`, records `unsynced`, TLS waits (certificate checks are never relaxed). A wrong but synchronized clock appears as certificate verify flags in `net_error` |
| Not yet | RTC adapter (planned behind the same provider boundary; DS3231 first): with a valid RTC, use RTC time at start and refresh from NTP when available. Daily RTC maintenance is proposed from CESH, not finalized |

Tests: `test/host/test_time.cpp`, built with record timestamps on and off
(simulated clock). The ESP32 SNTP provider is compiled against stubs in
`test/host/test_esp32_wifi.cpp`; it has not run on a board here.

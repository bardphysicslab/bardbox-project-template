# Counter with automatic UTC record timestamps

The isolated Wi-Fi demo, now with an increasing counter instead of the constant
42 ([#49](https://github.com/bardphysicslab/bardbox-project-template/issues/49)).
The sketch registers `demo.counter` (integer) and publishes the next count every
half second. There is no timestamp code in the sketch: once the board has
synchronized its clock over NTP, BardBox stamps each record with the UTC time it
was created at its recording interval (`recorded_utc`). Records made before the
first synchronization say `"recorded_utc_status":"unsynced"`. Each record keeps
the latest count at its boundary, so counts between boundaries are skipped by
design. Synthetic data only, never a sensor reading.

Build (compile only), from `software/firmware`:

```sh
pio run -e feather_s3_counter_test
```

It uses the same private `include/config.h` as the 42 demo (Wi-Fi, endpoint,
CA and token), including the `/demo/wifi42` endpoint path, so nothing private
changes. The receiver (`scripts/wifi42_receiver.py`, see
[the 42 demo](../wifi42/README.md)) accepts both this counter and 42 records still
queued on a board, and its page shows the latest count, when the board recorded
it (UTC), and when the Mac received it.

What is verified: the host demo (`test/host/run_host_demo.sh`) runs this sketch
with a simulated clock and delivery (one unsynced record, then UTC-stamped ones),
and the receiver tests. Not run on a board here.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "BardBoxBuffer.h"
#include "BardBoxConnectivity.h"
#include "BardBoxTime.h"

namespace bbcore {

// CANDIDATE delivery body (JSON), pending review in
// bardbox-project-template#36. It serializes one saved record exactly as it
// was recorded: the record's own attribution (UID, firmware, protocol) and
// channel schema, never the current configuration. It does not adopt any of
// the open API proposals (acquisition-time binding, freshness, grouping, the
// v2 wire, placeholder versions, raw provider values): acquisition time is
// "unknown" and hand-off fields keep their M1 meaning.
//
// When `sender` is given, the body also carries "device_status": the sending
// device's connectivity at send time (same field names and meanings as serial
// INFO), kept separate from the record, which describes measurement time.
//
// Returns the body length, or 0 if it does not fit in cap.
//
// The record carries "recorded_utc" (UTC when the record was created at its
// recording boundary, else null) and "recorded_utc_status" ("ntp",
// "unsynced", "disabled" or "not_recorded"); this is
// neither acquisition time ("acquired_time" stays "unknown") nor upload time.
// device_status also carries the sender's Time state when `time` is given.
size_t serializeDeliveryCandidate(const Record &record, const QueueToken &token, char *out, size_t cap,
                                  const ConnectivityStatus *sender = nullptr, const TimeStatus *time = nullptr);

// Largest body the candidate can produce for kMaxChannels channels with the
// longest names and units registration accepts (units may need escaping).
constexpr size_t kMaxDeliveryBody = 6144;

}  // namespace bbcore

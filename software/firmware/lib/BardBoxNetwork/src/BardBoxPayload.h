#pragma once

#include <stddef.h>
#include <stdint.h>

#include "BardBoxBuffer.h"

namespace bbcore {

// CANDIDATE delivery body (JSON), pending review in
// bardbox-project-template#36. It serializes one saved record exactly as it
// was recorded: the record's own attribution (UID, firmware, protocol) and
// channel schema, never the current configuration. It does not adopt any of
// the open API proposals (acquisition-time binding, freshness, grouping, the
// v2 wire, placeholder versions, raw provider values): acquisition time is
// "unknown" and hand-off fields keep their M1 meaning.
//
// Returns the body length, or 0 if it does not fit in cap.
size_t serializeDeliveryCandidate(const Record &record, const QueueToken &token, char *out, size_t cap);

// Largest body the candidate can produce for kMaxChannels channels with the
// longest names and units registration accepts (units may need escaping).
constexpr size_t kMaxDeliveryBody = 6144;

}  // namespace bbcore

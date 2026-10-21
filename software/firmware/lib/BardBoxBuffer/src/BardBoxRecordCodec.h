#pragma once

#include <stddef.h>
#include <stdint.h>

#include "BardBoxRecord.h"

namespace bbcore {

// Storage framing is separate from any serial wire form and from the record
// payload schema:
//   frame:   "BBQ1" | frame version (1) | payload length (u16) | CRC-32 (u32) | payload
//   payload: payload schema (candidate, not an accepted scientific schema) |
//            queue generation and sequence | record identity and monotonic
//            times | [schema 3: recorded-UTC status (u8) and UTC ms (i64)] |
//            original attribution (UID, firmware, wire protocol) | per-channel
//            schema (sensor, name, unit, type) | values, little-endian
// Schema 3 is written; schema 2 records (queued before this firmware) still
// decode, with recorded UTC unknown.
constexpr uint8_t kFrameVersion = 1;
constexpr uint8_t kPayloadSchemaCandidate = 3;
constexpr uint8_t kPayloadSchemaNoUtc = 2;
constexpr size_t kMaxFrameBytes = 11 + 30 + 9 + 3 + 32 + 32 + 16 + kMaxChannels * (19 + 3 + 23 + 23 + 15 + 1);

uint32_t crc32(const uint8_t *data, size_t len);

// Returns the frame length, or 0 if it does not fit.
size_t encodeRecordFrame(const Record &record, uint8_t *out, size_t cap);

enum class DecodeResult : uint8_t { Ok, BadFrame, BadChecksum, UnsupportedSchema };
DecodeResult decodeRecordFrame(const uint8_t *data, size_t len, Record &out);

}  // namespace bbcore

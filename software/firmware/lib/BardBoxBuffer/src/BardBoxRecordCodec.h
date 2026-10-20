#pragma once

#include <stddef.h>
#include <stdint.h>

#include "BardBoxRecord.h"

namespace bbcore {

// Storage framing is separate from any serial wire form and from the record
// payload schema:
//   frame:   "BBQ1" | frame version (1) | payload length (u16) | CRC-32 (u32) | payload
//   payload: payload schema (1 = M1 candidate, not an accepted scientific
//            schema) | record fields, little-endian
constexpr uint8_t kFrameVersion = 1;
constexpr uint8_t kPayloadSchemaM1Candidate = 1;
constexpr size_t kMaxFrameBytes = 11 + 1 + 17 + kMaxChannels * 19;

uint32_t crc32(const uint8_t *data, size_t len);

// Returns the frame length, or 0 if it does not fit.
size_t encodeRecordFrame(const Record &record, uint8_t *out, size_t cap);

enum class DecodeResult : uint8_t { Ok, BadFrame, BadChecksum, UnsupportedSchema };
DecodeResult decodeRecordFrame(const uint8_t *data, size_t len, Record &out);

}  // namespace bbcore

#pragma once

#include <stdint.h>

// Record data shared by the measurement cache, the Buffer and Serial output.
// BardBoxBuffer owns this record schema.

namespace bbcore {

constexpr uint8_t kMaxSensors = 4;
constexpr uint8_t kMaxChannels = 16;

enum class ValueKind : uint8_t { Float, Integer, Boolean };

// Per-channel outcome. Anything but Valid carries no value.
// NoSample: this sensor has never handed over a value.
enum class ChannelStatus : uint8_t { Valid, NoSample, InvalidType, NonFinite, NotRepresentable };

const char *channelStatusCode(ChannelStatus status);

struct ChannelValue {
  ValueKind kind = ValueKind::Float;
  ChannelStatus status = ChannelStatus::NoSample;
  double f = 0;
  int64_t i = 0;
  bool b = false;
};

// One channel as captured from the latest-reading cache.
//
// publishSeq and handoffAgeMs describe the most recent hand-off (publish call)
// for the channel's sensor. They are NOT acquisition identity or acquisition
// time: republishing old values gets a new publishSeq. Acquisition time is
// unknown unless an explicit acquisition-time binding supplies it (not in M1).
struct RecordChannel {
  ChannelValue value;
  bool hasSample = false;
  uint32_t publishSeq = 0;
  uint32_t handoffAgeMs = 0;
  bool acquiredKnown = false;    // always false in M1
  bool repeatedHandoff = false;  // same hand-off as in the previous record
};

// A completed full-node record created at a recording boundary.
struct Record {
  uint32_t bootId = 0;
  uint32_t seq = 0;         // record sequence within this boot
  uint32_t dueMs = 0;       // scheduled boundary (diagnostic)
  uint32_t capturedMs = 0;  // snapshot capture time (monotonic)
  uint8_t channelCount = 0;
  RecordChannel channels[kMaxChannels];
};

// Implemented by the owner of the latest-reading cache. Copies one consistent
// snapshot of every declared channel and reports the capture time it read
// while holding its lock. Returns the number of channels copied.
class SnapshotSource {
 public:
  virtual ~SnapshotSource() {}
  virtual uint8_t snapshotChannels(RecordChannel *out, uint32_t &capturedMs) = 0;
};

}  // namespace bbcore

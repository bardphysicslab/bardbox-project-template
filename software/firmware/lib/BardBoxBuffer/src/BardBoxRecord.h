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

// Original meaning of one channel, stored with every record so a later
// change of sensors, names, units or order cannot reinterpret saved values.
struct ChannelSchema {
  char sensor[24] = {};
  char name[24] = {};
  char unit[16] = {};
  ValueKind type = ValueKind::Float;
};

// Original attribution, stored with every record.
struct RecordAttribution {
  char uid[33] = {};
  char firmware[33] = {};
  char protocol[17] = {};  // wire protocol in use when recorded (a candidate)
};

// Where a record's UTC timestamp came from. RTC is reserved for a later adapter.
enum class UtcSourceKind : uint8_t { Unknown = 0, Ntp = 1 };

// A completed full-node record created at a recording boundary.
//
// Recorded-UTC status of a record, fixed when the record is created and
// stored with it (template#49): Ntp (recordedUtcMs valid), Unsynced (enabled,
// but no valid UTC yet), Disabled (project turned record timestamps off), or
// NotRecorded (saved by firmware that had no record timestamps; schema 2).
enum class RecordedUtc : uint8_t { Unsynced = 0, Ntp = 1, Disabled = 2, NotRecorded = 3 };
const char *recordedUtcName(RecordedUtc status);

// recordedUtcMs is the UTC time when this record was created at its boundary
// (not acquisition time, not upload time). It is set once, stored with the
// record, and never relabelled later.
struct Record {
  uint32_t bootId = 0;
  uint32_t seq = 0;         // record sequence within this boot
  uint32_t dueMs = 0;       // scheduled boundary (diagnostic)
  uint32_t capturedMs = 0;  // snapshot capture time (monotonic)
  RecordedUtc recordedUtc = RecordedUtc::Unsynced;
  int64_t recordedUtcMs = 0;  // milliseconds since 1970-01-01T00:00:00Z, when recordedUtc == Ntp
  uint32_t queueGeneration = 0;  // set when persisted; with queueSeq, the delivery token
  uint32_t queueSeq = 0;
  RecordAttribution attribution;
  uint8_t channelCount = 0;
  ChannelSchema schema[kMaxChannels];
  RecordChannel channels[kMaxChannels];
};

// Current UTC, implemented by the Time owner. Returns false (and no value)
// unless a valid time source has set the clock.
class UtcSource {
 public:
  virtual ~UtcSource() {}
  virtual bool utcNow(int64_t &epochMs, UtcSourceKind &source) = 0;
};

// Implemented by the owner of the latest-reading cache. Copies one consistent
// snapshot of every declared channel and reports the capture time it read
// while holding its lock. Returns the number of channels copied.
class SnapshotSource {
 public:
  virtual ~SnapshotSource() {}
  virtual uint8_t snapshotChannels(RecordChannel *out, uint32_t &capturedMs) = 0;
  // Describes every declared channel (fixed after registration closes).
  virtual void describeChannels(ChannelSchema *out) = 0;
};

}  // namespace bbcore

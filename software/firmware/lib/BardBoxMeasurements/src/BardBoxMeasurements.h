#pragma once

#include <stddef.h>
#include <stdint.h>
#include <type_traits>

#include "BardBoxRecord.h"

// Measurement type declared at registration. Omitted means Float.
enum class MeasurementType : uint8_t { Float, Integer, Boolean };

namespace bbcore {

// One positional publish argument, recorded with its C++ kind; no guessing.
struct ValueArg {
  enum class Kind : uint8_t { None, Floating, Signed, Unsigned, Boolean } kind = Kind::None;
  double f = 0;
  int64_t i = 0;
  uint64_t u = 0;
  bool b = false;

  ValueArg() {}
  ValueArg(float v) : kind(Kind::Floating), f(v) {}
  ValueArg(double v) : kind(Kind::Floating), f(v) {}
  ValueArg(bool v) : kind(Kind::Boolean), b(v) {}
  template <typename T,
            typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value &&
                                        std::is_signed<T>::value,
                                    int>::type = 0>
  ValueArg(T v) : kind(Kind::Signed), i(static_cast<int64_t>(v)) {}
  template <typename T,
            typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value &&
                                        !std::is_signed<T>::value,
                                    int>::type = 0>
  ValueArg(T v) : kind(Kind::Unsigned), u(static_cast<uint64_t>(v)) {}
};

struct PublishOutcome {
  bool accepted = false;       // false: nothing was cached
  uint8_t faultyChannels = 0;  // channels cached as null with a fault code
  const char *reason = "";     // why a publish was not accepted
};

// Converts one argument to a declared type. Integer-to-Float is the only
// conversion; nothing is truncated, clamped or zero-filled.
ChannelValue convertValue(MeasurementType type, const ValueArg &arg);

// Owns sensor and measurement registration, publish checks and the
// latest-reading cache. Checks: value count, declared type and finiteness.
// M1 has no per-measurement bounds.
class BardBoxMeasurements : public SnapshotSource {
 public:
  // Registration (before freeze). Returns -1 / false and counts an error.
  int addSensor(const char *name);
  bool addMeasurement(uint8_t sensor, const char *name, const char *unit, MeasurementType type);
  // Closes registration and enables locking for concurrent use.
  bool freeze();

  // Caches already-acquired values for one sensor. handoffMs is the monotonic
  // time of this publish call, kept only as hand-off metadata.
  PublishOutcome publish(uint8_t sensor, const ValueArg *args, size_t count);

  uint8_t snapshotChannels(RecordChannel *out, uint32_t &capturedMs) override;

  uint8_t sensorCount() const { return sensorCount_; }
  uint8_t channelCount() const { return channelCount_; }
  const char *sensorName(uint8_t s) const { return sensors_[s].name; }
  const char *channelName(uint8_t c) const { return channels_[c].name; }
  const char *channelUnit(uint8_t c) const { return channels_[c].unit; }
  uint8_t channelSensor(uint8_t c) const { return channels_[c].sensor; }
  uint8_t registrationErrors() const { return registrationErrors_; }

 private:
  struct Sensor {
    char name[24] = {};
    uint8_t firstChannel = 0;
    uint8_t channelCount = 0;
    bool hasSample = false;
    uint32_t publishSeq = 0;
    uint32_t handoffMs = 0;
  };
  struct Channel {
    char name[24] = {};
    char unit[16] = {};
    MeasurementType type = MeasurementType::Float;
    uint8_t sensor = 0;
    ChannelValue cached;
  };

  void lock();
  void unlock();

  Sensor sensors_[kMaxSensors];
  Channel channels_[kMaxChannels];
  uint8_t sensorCount_ = 0;
  uint8_t channelCount_ = 0;
  uint8_t registrationErrors_ = 0;
  bool frozen_ = false;
  void *mutex_ = nullptr;
};

}  // namespace bbcore

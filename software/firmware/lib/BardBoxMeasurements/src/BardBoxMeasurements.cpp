#include "BardBoxMeasurements.h"

#include <Arduino.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

#if defined(ARDUINO) && defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#define BARDBOX_MEASUREMENTS_RTOS 1
#endif

namespace bbcore {

namespace {

bool validName(const char *s, size_t max) {
  if (!s || !*s || strlen(s) >= max) return false;
  for (const char *p = s; *p; ++p)
    if (!(islower(static_cast<unsigned char>(*p)) || isdigit(static_cast<unsigned char>(*p)) || *p == '_'))
      return false;
  return true;
}

bool validUnit(const char *s, size_t max) {
  if (!s || strlen(s) >= max) return false;
  for (const char *p = s; *p; ++p)
    if (*p <= ' ' || *p > '~' || *p == ',' || *p == '|' || *p == '=') return false;
  return true;
}

ValueKind kindOf(MeasurementType type) {
  switch (type) {
    case MeasurementType::Integer: return ValueKind::Integer;
    case MeasurementType::Boolean: return ValueKind::Boolean;
    default: return ValueKind::Float;
  }
}

}  // namespace

ChannelValue convertValue(MeasurementType type, const ValueArg &arg) {
  ChannelValue v;
  v.kind = kindOf(type);
  v.status = ChannelStatus::InvalidType;
  switch (type) {
    case MeasurementType::Float:
      if (arg.kind == ValueArg::Kind::Floating) {
        v.f = arg.f;
        v.status = isfinite(arg.f) ? ChannelStatus::Valid : ChannelStatus::NonFinite;
      } else if (arg.kind == ValueArg::Kind::Signed) {
        v.f = static_cast<double>(arg.i);
        v.status = ChannelStatus::Valid;
      } else if (arg.kind == ValueArg::Kind::Unsigned) {
        v.f = static_cast<double>(arg.u);
        v.status = ChannelStatus::Valid;
      }
      break;
    case MeasurementType::Integer:
      if (arg.kind == ValueArg::Kind::Signed) {
        v.i = arg.i;
        v.status = ChannelStatus::Valid;
      } else if (arg.kind == ValueArg::Kind::Unsigned) {
        if (arg.u > static_cast<uint64_t>(INT64_MAX)) {
          v.status = ChannelStatus::NotRepresentable;
        } else {
          v.i = static_cast<int64_t>(arg.u);
          v.status = ChannelStatus::Valid;
        }
      }
      break;
    case MeasurementType::Boolean:
      if (arg.kind == ValueArg::Kind::Boolean) {
        v.b = arg.b;
        v.status = ChannelStatus::Valid;
      }
      break;
  }
  if (v.status != ChannelStatus::Valid) {
    v.f = 0;
    v.i = 0;
    v.b = false;
  }
  return v;
}

int BardBoxMeasurements::addSensor(const char *name) {
  bool ok = !frozen_ && sensorCount_ < kMaxSensors && validName(name, sizeof sensors_[0].name);
  for (uint8_t s = 0; ok && s < sensorCount_; ++s)
    if (strcmp(sensors_[s].name, name) == 0) ok = false;
  if (!ok) {
    ++registrationErrors_;
    return -1;
  }
  strcpy(sensors_[sensorCount_].name, name);
  return sensorCount_++;
}

bool BardBoxMeasurements::addMeasurement(uint8_t sensor, const char *name, const char *unit,
                                         MeasurementType type) {
  bool ok = !frozen_ && sensor < sensorCount_ && channelCount_ < kMaxChannels &&
            validName(name, sizeof channels_[0].name) && validUnit(unit, sizeof channels_[0].unit);
  // Measurements of different sensors may be declared in any interleaving;
  // each sensor keeps its own declared order for positional publish.
  for (uint8_t k = 0; ok && k < sensors_[sensor].channelCount; ++k)
    if (strcmp(channels_[sensors_[sensor].channels[k]].name, name) == 0) ok = false;
  if (!ok) {
    ++registrationErrors_;
    return false;
  }
  Channel &ch = channels_[channelCount_++];
  strcpy(ch.name, name);
  strcpy(ch.unit, unit);
  ch.type = type;
  ch.sensor = sensor;
  ch.cached.kind = kindOf(type);
  Sensor &s = sensors_[sensor];
  s.channels[s.channelCount++] = static_cast<uint8_t>(channelCount_ - 1);
  return true;
}

bool BardBoxMeasurements::freeze() {
  frozen_ = true;
#if defined(BARDBOX_MEASUREMENTS_RTOS)
  if (!mutex_) mutex_ = xSemaphoreCreateMutex();
  return mutex_ != nullptr;
#else
  return true;
#endif
}

PublishOutcome BardBoxMeasurements::publish(uint8_t sensor, const ValueArg *args, size_t count) {
  PublishOutcome result;
  if (sensor >= sensorCount_) {
    result.reason = "invalid_sensor";
    return result;
  }
  Sensor &s = sensors_[sensor];
  if (count != s.channelCount) {
    result.reason = "wrong_count";  // the cache is left unchanged
    return result;
  }
  ChannelValue converted[kMaxChannels];
  for (uint8_t k = 0; k < s.channelCount; ++k) {
    converted[k] = convertValue(channels_[s.channels[k]].type, args[k]);
    if (converted[k].status != ChannelStatus::Valid) ++result.faultyChannels;
  }
  lock();
  for (uint8_t k = 0; k < s.channelCount; ++k) channels_[s.channels[k]].cached = converted[k];
  ++s.publishSeq;
  s.hasSample = true;
  s.handoffMs = millis();
  unlock();
  result.accepted = true;
  return result;
}

uint8_t BardBoxMeasurements::snapshotChannels(RecordChannel *out, uint32_t &capturedMs) {
  lock();
  capturedMs = millis();  // read while holding the lock
  for (uint8_t c = 0; c < channelCount_; ++c) {
    const Sensor &s = sensors_[channels_[c].sensor];
    RecordChannel &ch = out[c];
    ch = RecordChannel();
    ch.value.kind = channels_[c].cached.kind;
    if (!s.hasSample) continue;  // status stays NoSample
    ch.hasSample = true;
    ch.publishSeq = s.publishSeq;
    ch.handoffAgeMs = capturedMs - s.handoffMs;  // hand-off happened before capture
    ch.value = channels_[c].cached;
  }
  unlock();
  return channelCount_;
}

void BardBoxMeasurements::lock() {
#if defined(BARDBOX_MEASUREMENTS_RTOS)
  if (mutex_) xSemaphoreTake(static_cast<SemaphoreHandle_t>(mutex_), portMAX_DELAY);
#else
  (void)mutex_;  // single-threaded hosts need no lock
#endif
}

void BardBoxMeasurements::unlock() {
#if defined(BARDBOX_MEASUREMENTS_RTOS)
  if (mutex_) xSemaphoreGive(static_cast<SemaphoreHandle_t>(mutex_));
#endif
}

}  // namespace bbcore

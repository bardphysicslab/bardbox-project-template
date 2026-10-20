#pragma once

// BardBoxInterface: the one student-facing BardBox object. It wires the
// separate owners together and implements none of them:
//   BardBoxMeasurements - registration, publish checks, latest-reading cache
//   BardBoxBuffer       - recording interval, boundaries, records, admission
//   BardBoxSerial       - commands and every wire form
//   BardBoxNetwork      - network configuration and status
//
// Milestone M1: no persistent storage, network delivery or OTA yet.

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "BardBoxBuffer.h"
#include "BardBoxInterfaceConfig.h"
#include "BardBoxMeasurements.h"
#include "BardBoxNetwork.h"
#include "BardBoxSerial.h"

// Student code must define FIRMWARE_VERSION before including this header.
#if !defined(BARDBOX_INTERFACE_IMPLEMENTATION) && !defined(FIRMWARE_VERSION)
#error "Please set FIRMWARE_VERSION in main.cpp."
#endif

using PublishResult = bbcore::PublishOutcome;

class BardBoxInterface;

// A logical sensor handle returned by BardBoxInterface::addSensor.
class BardBoxSensor {
 public:
  BardBoxSensor() {}
  bool valid() const { return owner_ != nullptr; }

  // Declares the next measurement of this sensor. Omit type for FLOAT.
  bool addMeasurement(const char *name, const char *unit,
                      MeasurementType type = MeasurementType::Float);

  // Hands over already-acquired values in declared measurement order.
  template <typename... Values>
  PublishResult publish(Values... values) {
    const bbcore::ValueArg args[sizeof...(Values) + 1] = {bbcore::ValueArg(values)...,
                                                           bbcore::ValueArg()};
    return publishValues(args, sizeof...(Values));
  }

 private:
  friend class BardBoxInterface;
  BardBoxSensor(BardBoxInterface *owner, uint8_t index) : owner_(owner), index_(index) {}
  PublishResult publishValues(const bbcore::ValueArg *args, size_t count);

  BardBoxInterface *owner_ = nullptr;
  uint8_t index_ = 0;
};

class BardBoxInterface : private bbcore::SerialDataSource {
 public:
  // Creates and registers a sensor once; invalid handle on error.
  BardBoxSensor addSensor(const char *name);

  // Starts BardBox, including internal service that keeps running while
  // student code waits in delay(). No arguments. Defined below, only in
  // translation units that see FIRMWARE_VERSION.
  inline bool begin();

  // Optional: validated effective public settings. Supported name:
  // "recording_interval_ms" (milliseconds).
  bool getSetting(const char *name, uint32_t &value) const;

  uint8_t registrationErrors() const { return measurements_.registrationErrors(); }

  // ---- Internal: not part of the student workflow ----
  bool beginWithVersion(const char *firmwareVersion);
  bool beginWithPort(Stream &port, const char *firmwareVersion, bool startServiceTask);
  // Storage for the Buffer; set before begin. ESP32 uses LittleFS by default.
  void useStorage(bbcore::StorageAdapter *storage) { storage_ = storage; }
  bbcore::BardBoxBuffer &bufferForDelivery() { return buffer_; }  // future Network owner
  // Hosts have no hardware random source; tests and demos set a boot ID.
  void setHostBootId(uint32_t bootId) { hostBootId_ = bootId; }
  void runServiceStep();  // one bounded Serial step, then one recording step

 private:
  friend class BardBoxSensor;

  // SerialDataSource: forwards to the owners.
  void status(bbcore::SerialStatus &out) override;
  uint8_t sensorCount() override { return measurements_.sensorCount(); }
  const char *sensorName(uint8_t s) override { return measurements_.sensorName(s); }
  uint8_t channelCount() override { return measurements_.channelCount(); }
  const char *channelName(uint8_t c) override { return measurements_.channelName(c); }
  uint8_t channelSensor(uint8_t c) override { return measurements_.channelSensor(c); }
  bool latestRecord(bbcore::Record &out) override { return buffer_.latest(out); }
  uint8_t liveChannels(bbcore::RecordChannel *out, uint32_t &nowMs) override {
    return measurements_.snapshotChannels(out, nowMs);
  }

  bbcore::BardBoxMeasurements measurements_;
  bbcore::BardBoxBuffer buffer_;
  bbcore::BardBoxSerial serial_;
  bbcore::BardBoxNetwork network_;
  bbcore::StorageAdapter *storage_ = nullptr;
  uint32_t hostBootId_ = 0x5eed0001u;
  bool begun_ = false;
  const char *firmwareVersion_ = "";
  uint32_t bootId_ = 0;
};

// begin() is defined only where student code includes this header, so every
// definition is identical (one FIRMWARE_VERSION per build). The implementation
// file declares but never defines or calls it.
#if !defined(BARDBOX_INTERFACE_IMPLEMENTATION)
static_assert(sizeof(FIRMWARE_VERSION) > 1, "Please set FIRMWARE_VERSION in main.cpp.");
static_assert(sizeof(FIRMWARE_VERSION) <= 33, "FIRMWARE_VERSION must be at most 32 characters.");
inline bool BardBoxInterface::begin() { return beginWithVersion(FIRMWARE_VERSION); }
#endif

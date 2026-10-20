#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "BardBoxRecord.h"
#include "BardBoxSerialConfig.h"

namespace bbcore {

// Builds one reply line on the serial port.
class SerialLine {
 public:
  explicit SerialLine(Stream &out) : out_(out) {}
  SerialLine &text(const char *s);
  SerialLine &uint(uint32_t v);
  SerialLine &integer(int64_t v);
  SerialLine &real(double v);
  SerialLine &hex32(uint32_t v);
  void end();

 private:
  Stream &out_;
};

struct SerialStatus {
  const char *uid = "";
  const char *firmware = "";
  uint32_t bootId = 0;
  uint32_t recordingIntervalS = 0;
  uint8_t buffered = 0;
  uint32_t backpressure = 0;
  uint32_t skippedBoundaries = 0;
  const char *storage = "";
  const char *network = "";
  uint8_t registrationErrors = 0;
};

// What Serial reports. BardBoxSerial owns parsing and every wire form.
class SerialDataSource {
 public:
  virtual ~SerialDataSource() {}
  virtual void status(SerialStatus &out) = 0;
  virtual uint8_t sensorCount() = 0;
  virtual const char *sensorName(uint8_t sensor) = 0;
  virtual uint8_t channelCount() = 0;
  virtual const char *channelName(uint8_t channel) = 0;
  virtual uint8_t channelSensor(uint8_t channel) = 0;
  virtual bool latestRecord(Record &out) = 0;
  virtual uint8_t liveChannels(RecordChannel *out, uint32_t &nowMs) = 0;
};

enum class SerialCommand : uint8_t { Empty, Info, Header, Read, ReadLive, Ping, Unknown };

// Case-insensitive; ignores surrounding spaces.
SerialCommand parseSerialCommand(const char *line);

// Wire forms. INFO keeps the existing "OK INFO uid= fw= sensors=" prefix and
// adds fields. HEADER, READ and READ_LIVE use a v2 CANDIDATE framing pending
// review (bardbox-project-template#36); legacy v1 parsers see "v2" and must
// not reinterpret it.
class BardBoxSerial {
 public:
  static constexpr size_t kMaxLine = 64;  // longer lines are discarded
  static constexpr size_t kMaxBytesPerStep = 256;

  template <typename Port>
  void beginPort(Port &port, SerialDataSource &source) {
    port.begin(BARDBOX_SERIAL_BAUD);
    begin(port, source);
  }
  void begin(Stream &port, SerialDataSource &source);

  // Bounded step: reads at most kMaxBytesPerStep bytes and answers at most
  // maxCommands complete lines.
  void service(uint8_t maxCommands = 1);

  uint32_t discardedLines() const { return discarded_; }
  static uint32_t configuredBaud() { return BARDBOX_SERIAL_BAUD; }

 private:
  void handle(const char *line);
  void writeInfo(SerialLine &line);
  void writeHeader(SerialLine &line);
  void writeRead(SerialLine &line);
  void writeReadLive(SerialLine &line);
  void writeChannelName(SerialLine &line, uint8_t channel);
  static void writeValue(SerialLine &line, const ChannelValue &value);

  Stream *port_ = nullptr;
  SerialDataSource *source_ = nullptr;
  char buf_[kMaxLine + 1] = {};
  size_t len_ = 0;
  bool overflow_ = false;
  uint32_t discarded_ = 0;
};

}  // namespace bbcore

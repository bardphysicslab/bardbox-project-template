#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "BardBoxRecord.h"
#include "BardBoxSettings.h"
#include "BardBoxSerialConfig.h"

namespace bbcore {

// Builds one complete reply line in a bounded buffer owned by BardBoxSerial.
// If the reply does not fit, the whole reply becomes "ERR REPLY_TOO_LONG".
class SerialLine {
 public:
  SerialLine(char *buf, size_t cap) : buf_(buf), cap_(cap) {}
  SerialLine &text(const char *s);
  SerialLine &uint(uint32_t v);
  SerialLine &integer(int64_t v);
  SerialLine &real(double v);
  SerialLine &hex32(uint32_t v);
  void end();
  size_t length() const { return overflow_ ? 0 : len_; }
  bool overflowed() const { return overflow_; }

 private:
  char *buf_;
  size_t cap_;
  size_t len_ = 0;
  bool overflow_ = false;
};

struct SerialStatus {
  const char *uid = "";
  const char *firmware = "";
  uint32_t bootId = 0;
  uint32_t recordingIntervalS = 0;
  const char *storage = "";
  bool durable = false;
  uint32_t stored = 0;
  uint32_t capacity = 0;
  uint32_t evicted = 0;
  uint32_t pendingRam = 0;
  uint32_t lostRam = 0;
  uint32_t writeFailures = 0;
  uint32_t interrupted = 0;
  uint32_t quarantined = 0;
  uint32_t skippedBoundaries = 0;
  const char *queueState = "ok";
  uint32_t hidden = 0;
  uint32_t unreadable = 0;
  uint32_t metaRebuilt = 0;
  bool lossHistoryKnown = true;
  const char *network = "";
  uint32_t netAttempts = 0;
  uint32_t netAcked = 0;
  uint32_t netFailed = 0;
  uint32_t netTimeouts = 0;
  uint32_t netRetryMs = 0;
  char netLast[24] = "none";
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

enum class SerialCommand : uint8_t { Empty, Info, Header, Read, ReadLive, Ping, Config, Unknown };

// Wire protocol name stamped on records; a candidate pending review.
constexpr const char *kWireProtocol = "v2-candidate";

// Case-insensitive command word; ignores surrounding spaces. Any line whose
// first word starts with CONFIG_ is a settings command (arguments follow).
SerialCommand parseSerialCommand(const char *line);

// Wire forms. INFO keeps the existing "OK INFO uid= fw= sensors=" prefix and
// adds fields. HEADER, READ and READ_LIVE use a v2 CANDIDATE framing pending
// review (bardbox-project-template#36); legacy v1 parsers see "v2" and must
// not reinterpret it.
class BardBoxSerial {
 public:
  // Longer lines are discarded. Fits CONFIG_SET with a 63-byte password in hex.
  static constexpr size_t kMaxLine = 160;
  static constexpr size_t kMaxBytesPerStep = 256;
  static constexpr size_t kMaxReply = 768;

  template <typename Port>
  void beginPort(Port &port, SerialDataSource &source) {
    port.begin(BARDBOX_SERIAL_BAUD);
    begin(port, source);
  }
  void begin(Stream &port, SerialDataSource &source);
  // Enables the CONFIG_* settings commands (capability config/1). Without it
  // they answer as unknown commands.
  void useSettings(BardBoxSettings &settings) { settings_ = &settings; }

  // Bounded step. Output never blocks: a pending reply is written only as far
  // as the port reports room (partial writes resume next step), and no new
  // command is read while a reply is pending. Then reads at most
  // kMaxBytesPerStep bytes and answers at most maxCommands lines.
  void service(uint8_t maxCommands = 1);

  bool replyPending() const { return outPos_ < outLen_; }
  uint32_t discardedLines() const { return discarded_; }
  uint32_t stalledSteps() const { return stalled_; }
  static uint32_t configuredBaud() { return BARDBOX_SERIAL_BAUD; }

 private:
  void handle(const char *line);
  bool flush();  // true when no reply is pending
  void writeInfo(SerialLine &line);
  void writeHeader(SerialLine &line);
  void writeRead(SerialLine &line);
  void writeReadLive(SerialLine &line);
  void writeChannelName(SerialLine &line, uint8_t channel);
  void handleConfig(const char *line, SerialLine &out);
  static void writeValue(SerialLine &line, const ChannelValue &value);

  Stream *port_ = nullptr;
  SerialDataSource *source_ = nullptr;
  BardBoxSettings *settings_ = nullptr;
  char buf_[kMaxLine + 1] = {};
  size_t len_ = 0;
  bool overflow_ = false;
  uint32_t discarded_ = 0;
  char out_[kMaxReply] = {};
  size_t outLen_ = 0;
  size_t outPos_ = 0;
  uint32_t stalled_ = 0;
  Record record_;  // scratch for READ/INFO; keeps large records off the stack
};

}  // namespace bbcore

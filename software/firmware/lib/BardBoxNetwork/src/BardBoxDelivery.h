#pragma once

#include <stddef.h>
#include <stdint.h>

#include "BardBoxBuffer.h"
#include "BardBoxConnectivity.h"
#include "BardBoxPayload.h"

namespace bbcore {

// Network state and counters, published by the Network owner for INFO.
enum class NetworkState : uint8_t {
  Disabled,       // BARDBOX_NETWORK_ENABLED 0: serial-only
  Unconfigured,   // enabled, but no endpoint configured
  NoTransport,    // enabled and configured, but no transport adapter in this build
  Idle,           // waiting for the next queue check
  WaitingRecord,  // asked the Buffer owner for the oldest record
  Delivering,     // an attempt is in progress in the transport
  WaitingAck,     // accepted; asked the Buffer owner to remove that exact record
  Backoff,        // last attempt failed; retry after the backoff
};
const char *networkStateName(NetworkState state);

struct NetworkStats {
  NetworkState state = NetworkState::Disabled;
  uint32_t attempts = 0;
  uint32_t acked = 0;            // accepted and removed from the queue
  uint32_t failed = 0;           // attempts or steps that ended in a retry
  uint32_t timeouts = 0;
  uint32_t consecutiveFailures = 0;
  uint32_t retryInMs = 0;        // current backoff (0 when not backing off)
  int lastHttp = 0;              // last HTTP status seen (0 = none)
  char lastOutcome[24] = "none";  // copied, so readers never see a changing buffer
  ConnectivityStatus link;        // Wi-Fi diagnostics, refreshed about once a second
};

// Bounded hand-off between the Network owner and the single Buffer owner. The
// Network owner never calls the Buffer or its codec: it posts one request at
// a time; the Buffer owner answers on its own task. One request slot and one
// result slot. The lock is held only to copy a slot, never during transport,
// storage or serialization.
struct DeliveryRequest {
  enum class Kind : uint8_t { None, Oldest, Ack };
  Kind kind = Kind::None;
  uint32_t id = 0;
  QueueToken token;  // Ack: the exact record to remove
};

struct DeliveryResult {
  enum class Kind : uint8_t { None, Record, Empty, ReadFailed, TooLarge, Ack };
  Kind kind = Kind::None;
  uint32_t id = 0;   // the request this answers
  QueueToken token;
  BardBoxBuffer::AckResult ack = BardBoxBuffer::AckResult::Empty;
  size_t bodyLen = 0;
  char body[kMaxDeliveryBody] = {};
};

class DeliveryMailbox {
 public:
  void begin();  // creates the lock (ESP32); call once before use

  // Network owner. post fails while a request or result is outstanding.
  bool post(const DeliveryRequest &request);
  bool takeResult(DeliveryResult &out);
  void publishStats(const NetworkStats &stats);

  // Buffer owner.
  bool takeRequest(DeliveryRequest &out);
  void postResult(const DeliveryResult &result);

  // Any task: the last published network status.
  NetworkStats stats();

 private:
  void lock();
  void unlock();
  void *mutex_ = nullptr;
  bool hasRequest_ = false;
  bool hasResult_ = false;
  DeliveryRequest request_;
  DeliveryResult result_;
  NetworkStats stats_;
};

// Runs on the Buffer owner's task (the BardBox service task): answers at most
// one request per step with peekOldest or acknowledge, so the Buffer and its
// codec keep a single owner.
class DeliveryBufferSide {
 public:
  void step(BardBoxBuffer &buffer, DeliveryMailbox &mailbox);

 private:
  Record record_;
  DeliveryResult result_;
};

}  // namespace bbcore

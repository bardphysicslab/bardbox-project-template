#pragma once

#include <stddef.h>
#include <stdint.h>

namespace bbcore {

// Delivery phases. The Network owner gives each one a finite deadline.
enum class TransportPhase : uint8_t {
  Idle, Associating, Resolving, Connecting, Tls, Sending, AwaitingResponse, Done
};
const char *transportPhaseName(TransportPhase phase);

// How an attempt ended. Only an HTTP 2xx that the transport received for the
// current attempt, from the verified configured endpoint, is an
// acknowledgement; everything else leaves the record queued.
struct TransportOutcome {
  enum class Kind : uint8_t { None, HttpStatus, ConnectionFailed, Malformed, NoResponse };
  Kind kind;
  int httpStatus;
  uint32_t attemptId;     // the attempt this outcome belongs to
  bool endpointVerified;  // response came from the configured, verified endpoint
  TransportOutcome() : kind(Kind::None), httpStatus(0), attemptId(0), endpointVerified(false) {}
};

// Injected transport. Every call must return promptly (bounded work, no
// blocking wait); the Network owner calls step() repeatedly and enforces the
// phase deadlines. A real Wi-Fi/HTTPS adapter is not part of this milestone.
class Transport {
 public:
  virtual ~Transport() {}
  virtual const char *name() const = 0;
  // Optional bounded connection maintenance, including before any record exists.
  virtual void service(uint32_t nowMs) { (void)nowMs; }
  // Begins one attempt. The transport must copy what it needs from body
  // before returning; the pointer is not valid afterwards.
  virtual bool start(uint32_t attemptId, const uint8_t *body, size_t len, uint32_t nowMs) = 0;
  // Bounded progress. Returns the current phase; when Done, outcome is set.
  virtual TransportPhase step(uint32_t nowMs, TransportOutcome &outcome) = 0;
  // Abandons the current attempt (after a deadline). Must be bounded.
  virtual void abort() = 0;
};

}  // namespace bbcore

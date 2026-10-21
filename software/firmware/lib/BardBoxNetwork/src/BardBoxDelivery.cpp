#include "BardBoxDelivery.h"

#include <string.h>

#if defined(ARDUINO) && defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#define BARDBOX_DELIVERY_RTOS 1
#endif

namespace bbcore {

const char *networkStateName(NetworkState state) {
  switch (state) {
    case NetworkState::Disabled: return "disabled";
    case NetworkState::Unconfigured: return "unconfigured";
    case NetworkState::NoTransport: return "no_transport";
    case NetworkState::Idle: return "idle";
    case NetworkState::WaitingRecord: return "waiting_record";
    case NetworkState::Delivering: return "delivering";
    case NetworkState::WaitingAck: return "waiting_ack";
    case NetworkState::Backoff: return "backoff";
  }
  return "unknown";
}

#if defined(BARDBOX_DELIVERY_RTOS)
static StaticSemaphore_t mailboxMutexStorage;
#endif

void DeliveryMailbox::begin() {
#if defined(BARDBOX_DELIVERY_RTOS)
  if (!mutex_) mutex_ = xSemaphoreCreateMutexStatic(&mailboxMutexStorage);  // one mailbox per device
#else
  (void)mutex_;  // hosts step both owners from one thread
#endif
}

void DeliveryMailbox::lock() {
#if defined(BARDBOX_DELIVERY_RTOS)
  if (mutex_) xSemaphoreTake(static_cast<SemaphoreHandle_t>(mutex_), portMAX_DELAY);
#endif
}

void DeliveryMailbox::unlock() {
#if defined(BARDBOX_DELIVERY_RTOS)
  if (mutex_) xSemaphoreGive(static_cast<SemaphoreHandle_t>(mutex_));
#endif
}

bool DeliveryMailbox::post(const DeliveryRequest &request) {
  lock();
  const bool ok = !hasRequest_ && !hasResult_;
  if (ok) {
    request_ = request;
    hasRequest_ = true;
  }
  unlock();
  return ok;
}

bool DeliveryMailbox::takeResult(DeliveryResult &out) {
  lock();
  const bool ok = hasResult_;
  if (ok) {
    out.kind = result_.kind;
    out.id = result_.id;
    out.token = result_.token;
    out.ack = result_.ack;
    out.bodyLen = result_.bodyLen;
    memcpy(out.body, result_.body, result_.bodyLen);
    hasResult_ = false;
  }
  unlock();
  return ok;
}

void DeliveryMailbox::publishStats(const NetworkStats &stats) {
  lock();
  stats_ = stats;
  unlock();
}

bool DeliveryMailbox::takeRequest(DeliveryRequest &out) {
  lock();
  const bool ok = hasRequest_;
  if (ok) {
    out = request_;
    hasRequest_ = false;
  }
  unlock();
  return ok;
}

void DeliveryMailbox::postResult(const DeliveryResult &result) {
  lock();
  result_.kind = result.kind;
  result_.id = result.id;
  result_.token = result.token;
  result_.ack = result.ack;
  result_.bodyLen = result.bodyLen;
  memcpy(result_.body, result.body, result.bodyLen);
  hasResult_ = true;
  unlock();
}

NetworkStats DeliveryMailbox::stats() {
  lock();
  const NetworkStats s = stats_;
  unlock();
  return s;
}

void DeliveryBufferSide::step(BardBoxBuffer &buffer, DeliveryMailbox &mailbox, const TimeStatus *time) {
  DeliveryRequest request;
  if (!mailbox.takeRequest(request)) return;
  result_.id = request.id;
  result_.bodyLen = 0;
  result_.token = QueueToken();
  if (request.kind == DeliveryRequest::Kind::Ack) {
    result_.kind = DeliveryResult::Kind::Ack;
    result_.token = request.token;
    result_.ack = buffer.acknowledge(request.token);
  } else if (buffer.peekOldest(record_, result_.token)) {
    // A copy of the saved record, with its original attribution and schema.
    const NetworkStats sender = mailbox.stats();  // connectivity at send time, separate from the record
    result_.bodyLen = serializeDeliveryCandidate(record_, result_.token, result_.body, sizeof result_.body, &sender.link, time);
    result_.kind = result_.bodyLen ? DeliveryResult::Kind::Record : DeliveryResult::Kind::TooLarge;
  } else {
    result_.kind = buffer.status().stored ? DeliveryResult::Kind::ReadFailed : DeliveryResult::Kind::Empty;
  }
  mailbox.postResult(result_);
}

}  // namespace bbcore

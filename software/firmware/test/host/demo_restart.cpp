// Host demo of durable recording across a simulated restart. Two BardBox
// instances share one in-memory storage; built with capacity 4. Simulated
// storage and restarts only: not flash, power-loss or hardware evidence.
#define FIRMWARE_VERSION "0.0.0-m2-host-demo"
#include <BardBoxInterface.h>
#include <stdio.h>

#include <string>

#include "FakeStorage.h"

uint32_t g_host_millis = 0;
HostStream Serial;
void (*g_host_service)(void *) = nullptr;
void *g_host_service_arg = nullptr;
bbcore::StorageAdapter *g_host_demo_storage = nullptr;
bbcore::Transport *g_host_demo_transport = nullptr;
bbcore::TimeSyncProvider *g_host_demo_time = nullptr;

static std::string ask(BardBoxInterface &box, HostStream &io, const char *cmd) {
  io.output.clear();
  io.input.append(cmd).append("\n");
  box.runServiceStep();
  std::string out = io.output;
  if (!out.empty() && out[out.size() - 1] == '\n') out.erase(out.size() - 1);
  return out;
}

static void show(const char *label, const std::string &s) { printf("%-28s %s\n", label, s.c_str()); }

static bbcore::Record shown;

static void head(BardBoxInterface &box, const char *label) {
  bbcore::QueueToken token;
  if (box.bufferForDelivery().peekOldest(shown, token))
    printf("%-28s token=%lu/%lu boot=%08lx record=%lu uid=%s %s.%s=%g %s\n", label,
           (unsigned long)token.generation, (unsigned long)token.seq, (unsigned long)shown.bootId,
           (unsigned long)shown.seq, shown.attribution.uid, shown.schema[0].sensor, shown.schema[0].name,
           shown.channels[0].value.f, shown.schema[0].unit);
  else
    printf("%-28s (empty)\n", label);
}

static const char *ackName(bbcore::BardBoxBuffer::AckResult a) {
  switch (a) {
    case bbcore::BardBoxBuffer::AckResult::Removed: return "Removed";
    case bbcore::BardBoxBuffer::AckResult::StaleToken: return "StaleToken (nothing removed)";
    case bbcore::BardBoxBuffer::AckResult::Empty: return "Empty";
    default: return "StorageError (record kept)";
  }
}

int main() {
  FakeStorage storage;
  uint32_t t = 1000;
  printf("== boot 1 (capacity %d records)\n", BARDBOX_BUFFER_CAPACITY_RECORDS);
  {
    BardBoxInterface box;
    HostStream io;
    g_host_millis = t;
    BardBoxSensor bme = box.addSensor("bme280");
    bme.addMeasurement("humidity", "%");
    box.useStorage(&storage);
    box.setHostBootId(0xb0070001u);
    box.beginWithPort(io, FIRMWARE_VERSION, false);
    for (int i = 1; i <= 3; ++i) {
      bme.publish(40.0f + i);
      t += 60000;
      g_host_millis = t;
      box.runServiceStep();
    }
    show("INFO", ask(box, io, "INFO"));
    head(box, "oldest");
    bbcore::QueueToken token;
    box.bufferForDelivery().peekOldest(shown, token);
    bbcore::QueueToken wrong = token;
    wrong.seq = 2;
    printf("%-28s %s\n", "ack(seq 2, not the head)", ackName(box.bufferForDelivery().acknowledge(wrong)));
    printf("%-28s %s\n", "ack(exact head token)", ackName(box.bufferForDelivery().acknowledge(token)));
    storage.failWrites = 1;  // next record cannot be written
    bme.publish(44.0f);
    t += 60000;
    g_host_millis = t;
    box.runServiceStep();
    show("after a failed write", ask(box, io, "INFO"));  // composed before this step's retry
    show("next step (retried)", ask(box, io, "INFO"));
    storage.files["/bbq/r.tmp"] = {1, 2, 3};  // simulate power lost mid-write of a later record
  }
  printf("== boot 2 (restart on the same storage)\n");
  {
    BardBoxInterface box;
    HostStream io;
    g_host_millis = 2000;  // monotonic clock restarts
    t = 2000;
    BardBoxSensor bme = box.addSensor("bme280");
    bme.addMeasurement("humidity", "%");
    box.useStorage(&storage);
    box.setHostBootId(0xb0070002u);
    box.beginWithPort(io, FIRMWARE_VERSION, false);
    show("INFO", ask(box, io, "INFO"));
    show("READ", ask(box, io, "READ"));
    head(box, "oldest (from boot 1)");
    for (int i = 5; i <= 8; ++i) {
      bme.publish(40.0f + i);
      t += 60000;
      g_host_millis = t;
      box.runServiceStep();
    }
    show("after 4 more records", ask(box, io, "INFO"));
    printf("delivery order:\n");
    bbcore::QueueToken token;
    while (box.bufferForDelivery().peekOldest(shown, token)) {
      printf("  token=%lu/%lu boot=%08lx record=%lu humidity=%g -> %s\n", (unsigned long)token.generation,
             (unsigned long)token.seq, (unsigned long)shown.bootId, (unsigned long)shown.seq,
             shown.channels[0].value.f, ackName(box.bufferForDelivery().acknowledge(token)));
    }
  }
  return 0;
}

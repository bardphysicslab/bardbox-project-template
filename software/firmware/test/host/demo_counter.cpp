// Runs the actual counter sketch through the API with a simulated clock and
// transport: unsynced records first, then UTC-stamped ones (template#49).
#include <Arduino.h>
#include "BardBoxTime.h"
#include "FakeStorage.h"
#include "FakeTransport.h"
#include <assert.h>
#include <stdio.h>
uint32_t g_host_millis=0;
HostStream Serial;
void (*g_host_service)(void *)=nullptr;
void *g_host_service_arg=nullptr;
struct DemoClock : bbcore::TimeSyncProvider {
  bool synced=false;
  void start(const char *, uint32_t) override {}
  bool utcNow(int64_t &ms) override { if(!synced) return false; ms=1792540800000LL+g_host_millis; return true; }
  uint32_t syncCount() override { return synced?1:0; }
  uint32_t lastSyncMonotonicMs() override { return 0; }
};
static FakeStorage storage;
static FakeTransport transport;
static DemoClock demoClock;
bbcore::StorageAdapter *g_host_demo_storage=&storage;
bbcore::Transport *g_host_demo_transport=&transport;
bbcore::TimeSyncProvider *g_host_demo_time=&demoClock;
void setup(); void loop();
int main() {
  setup();
  for(int i=0;i<12;++i) loop();   // 6 s: one record before the clock is synchronized
  demoClock.synced=true;
  for(int i=0;i<24;++i) loop();   // 12 s more: UTC-stamped records
  assert(transport.bodies.size()>=2);
  const std::string &first=transport.bodies.front(), &last=transport.bodies.back();
  assert(first.find("\"measurement\":\"counter\"")!=std::string::npos);
  assert(first.find("\"recorded_utc\":null,\"recorded_utc_status\":\"unsynced\"")!=std::string::npos);
  assert(last.find("\"recorded_utc\":\"2026-10-21T")!=std::string::npos);
  printf("counter sketch, before NTP:\n%s\nafter NTP:\n%s\n",first.c_str(),last.c_str());
}

// Runs the actual wifi42 sketch through the API with simulated transport.
#include <Arduino.h>
#include "FakeStorage.h"
#include "FakeTransport.h"
#include <assert.h>
#include <stdio.h>
uint32_t g_host_millis=0;
HostStream Serial;
void (*g_host_service)(void *)=nullptr;
void *g_host_service_arg=nullptr;
static FakeStorage storage;
static FakeTransport transport;
bbcore::StorageAdapter *g_host_demo_storage=&storage;
bbcore::Transport *g_host_demo_transport=&transport;
void setup(); void loop();
int main() {
  setup();
  for(int i=0;i<24;++i) loop();
  assert(!transport.bodies.empty());
  assert(transport.bodies[0].find("\"measurement\":\"answer\"")!=std::string::npos);
  assert(transport.bodies[0].find("\"value\":42")!=std::string::npos);
  printf("wifi42 sketch -> BardBox API -> saved record -> simulated delivery:\n%s\n",transport.bodies[0].c_str());
}

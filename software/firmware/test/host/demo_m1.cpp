// Host demo: runs the unmodified student sketch (src/main.cpp) with the
// SIMULATED BME280, simulated time and a scripted serial host. Evidence from
// this program is host/simulated only, never board or sensor evidence.
#include <Arduino.h>
#include <stdio.h>

#include <string>

#include "FakeStorage.h"
#include "BardBoxTransport.h"

uint32_t g_host_millis = 0;
HostStream Serial;
void (*g_host_service)(void *) = nullptr;
void *g_host_service_arg = nullptr;
static FakeStorage demoStorage;
bbcore::StorageAdapter *g_host_demo_storage = &demoStorage;
bbcore::Transport *g_host_demo_transport = nullptr;  // no endpoint: "unconfigured"

void setup();
void loop();

static void send(const char *cmd) {
  printf("[t=%6.1fs] > %s\n", g_host_millis / 1000.0, cmd);
  Serial.input.append(cmd).append("\n");
}

static void flush() {
  size_t start = 0, nl;
  while ((nl = Serial.output.find('\n', start)) != std::string::npos) {
    printf("[t=%6.1fs] < %s\n", g_host_millis / 1000.0, Serial.output.substr(start, nl - start).c_str());
    start = nl + 1;
  }
  Serial.output.erase(0, start);
}

int main() {
  setup();
  const char *startup[] = {"INFO", "HEADER", "READ", "READ_LIVE"};
  for (const char *c : startup) send(c);
  for (int i = 0; i < 260; ++i) {  // 260 loop() calls of 500 ms each: about 130 s
    if (i == 20) send("READ_LIVE");   // around the simulated NaN pressure read
    if (i == 125) { send("READ"); send("READ_LIVE"); }
    if (i == 245) { send("READ"); send("INFO"); }
    loop();
    flush();
  }
  return 0;
}

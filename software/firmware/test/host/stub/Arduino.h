#pragma once
// Minimal host stand-in for the Arduino API used by BardBox modules.
// Host tests only; not shipped to devices.
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include <string>

extern uint32_t g_host_millis;
inline uint32_t millis() { return g_host_millis; }

class Stream {
 public:
  virtual ~Stream() {}
  virtual int available() = 0;
  virtual int read() = 0;
  virtual size_t write(const uint8_t *data, size_t len) = 0;
};

// Scripted input and captured output for tests.
class HostStream : public Stream {
 public:
  std::string input;
  std::string output;
  size_t pos = 0;
  int available() override { return static_cast<int>(input.size() - pos); }
  int read() override { return pos < input.size() ? static_cast<unsigned char>(input[pos++]) : -1; }
  size_t write(const uint8_t *data, size_t len) override {
    output.append(reinterpret_cast<const char *>(data), len);
    return len;
  }
};

// Host demo support: the student sketch's Serial and delay(). delay() advances
// simulated time in 10 ms steps and runs the registered BardBox service step,
// standing in for the device's background service task.
extern HostStream Serial;
extern void (*g_host_service)(void *);
extern void *g_host_service_arg;
inline void delay(uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 10) {
    g_host_millis += 10;
    if (g_host_service) g_host_service(g_host_service_arg);
  }
}

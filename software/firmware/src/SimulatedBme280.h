#pragma once

#include <math.h>

// SIMULATED BME280 for the M1 demo. It produces slowly varying synthetic
// values and, every 20th pressure read, a NaN to show how BardBox stores an
// invalid channel without dropping healthy ones. It is not hardware evidence.
class SimulatedBme280 {
 public:
  void begin() { reads_ = 0; }
  float readHumidity() { return 43.0f + 2.0f * sinf(phase()); }
  float readTemperature() { return 21.5f + 0.5f * cosf(phase()); }
  float readPressure() {
    ++reads_;
    return (reads_ % 20 == 0) ? NAN : 100980.0f + 30.0f * sinf(phase());
  }

 private:
  float phase() const { return reads_ * 0.05f; }
  unsigned long reads_ = 0;
};

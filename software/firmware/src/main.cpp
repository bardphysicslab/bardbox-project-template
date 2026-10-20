// Student: enter your application firmware version here, for example "1.0.0".
// (A test build environment may supply one; see platformio.ini.)
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION ""
#endif

#include <Arduino.h>
#include <BardBoxInterface.h>

#include "SimulatedBme280.h"

BardBoxInterface bardbox;   // the one BardBox device
BardBoxSensor bme280;       // logical sensor handle
SimulatedBme280 bmeDriver;  // SIMULATED driver: replace with the real BME280 driver

void setup() {
  bme280 = bardbox.addSensor("bme280");
  bme280.addMeasurement("humidity", "%");  // FLOAT (type omitted)
  bme280.addMeasurement("temperature", "degC");
  bme280.addMeasurement("pressure", "Pa");
  bmeDriver.begin();  // hardware initialization stays application code
  bardbox.begin();    // no arguments; starts BardBox's internal service
}

void loop() {
  float humidity = bmeDriver.readHumidity();  // values acquired by the application
  float temperature = bmeDriver.readTemperature();
  float pressure = bmeDriver.readPressure();
  bme280.publish(humidity, temperature, pressure);  // declared order
  delay(500);  // BardBox keeps serving Serial and recording meanwhile
}

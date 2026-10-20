#define BARDBOX_INTERFACE_IMPLEMENTATION
#include "BardBoxInterface.h"

#include <string.h>

#if defined(ARDUINO) && defined(ESP_PLATFORM)
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#define BARDBOX_INTERFACE_RTOS 1
#endif

bool BardBoxSensor::addMeasurement(const char *name, const char *unit, MeasurementType type) {
  return owner_ && owner_->measurements_.addMeasurement(index_, name, unit, type);
}

PublishResult BardBoxSensor::publishValues(const bbcore::ValueArg *args, size_t count) {
  if (!owner_) {
    PublishResult r;
    r.reason = "invalid_sensor";
    return r;
  }
  return owner_->measurements_.publish(index_, args, count);
}

BardBoxSensor BardBoxInterface::addSensor(const char *name) {
  const int index = begun_ ? -1 : measurements_.addSensor(name);
  return index < 0 ? BardBoxSensor() : BardBoxSensor(this, static_cast<uint8_t>(index));
}

bool BardBoxInterface::getSetting(const char *name, uint32_t &value) const {
  if (name && strcmp(name, "recording_interval_ms") == 0) {
    value = bbcore::BardBoxBuffer::recordingIntervalMs();
    return true;
  }
  return false;
}

#if defined(BARDBOX_INTERFACE_RTOS)
static void serviceTask(void *arg) {
  BardBoxInterface *self = static_cast<BardBoxInterface *>(arg);
  for (;;) {
    self->runServiceStep();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
#endif

bool BardBoxInterface::beginWithVersion(const char *firmwareVersion) {
#if defined(BARDBOX_INTERFACE_RTOS)
  Serial.begin(bbcore::BardBoxSerial::configuredBaud());
  return beginWithPort(Serial, firmwareVersion, true);
#elif defined(BARDBOX_HOST_DEMO)
  // Host demo only: the stub's delay() runs the service step.
  g_host_service = [](void *self) { static_cast<BardBoxInterface *>(self)->runServiceStep(); };
  g_host_service_arg = this;
  return beginWithPort(Serial, firmwareVersion, false);
#else
  (void)firmwareVersion;
  return false;  // hosts call beginWithPort with their own stream
#endif
}

bool BardBoxInterface::beginWithPort(Stream &port, const char *firmwareVersion, bool startServiceTask) {
  if (begun_ || !firmwareVersion || !*firmwareVersion) return false;
  if (!measurements_.freeze()) return false;
  firmwareVersion_ = firmwareVersion;
#if defined(BARDBOX_INTERFACE_RTOS)
  bootId_ = esp_random();
#else
  bootId_ = 0x5eed0001u;  // fixed on hosts so tests are deterministic
#endif
  buffer_.begin(millis(), bootId_);
  network_.begin();
  serial_.begin(port, *this);
  begun_ = true;
#if defined(BARDBOX_INTERFACE_RTOS)
  if (startServiceTask && xTaskCreate(serviceTask, "bardbox", 6144, this, 1, nullptr) != pdPASS) {
    begun_ = false;  // no running service, so no Serial is promised
    return false;
  }
#else
  (void)startServiceTask;
#endif
  return measurements_.registrationErrors() == 0;
}

void BardBoxInterface::runServiceStep() {
  if (!begun_) return;
  serial_.service(1);
  buffer_.service(millis(), measurements_);
}

void BardBoxInterface::status(bbcore::SerialStatus &out) {
  out.uid = BARDBOX_DEVICE_UID;
  out.firmware = firmwareVersion_;
  out.bootId = bootId_;
  out.recordingIntervalS = BARDBOX_RECORDING_INTERVAL_S;
  out.buffered = buffer_.buffered();
  out.backpressure = buffer_.backpressureCount();
  out.skippedBoundaries = buffer_.skippedBoundaries();
  out.storage = "ram";
  out.network = network_.statusName();
  out.registrationErrors = measurements_.registrationErrors();
}

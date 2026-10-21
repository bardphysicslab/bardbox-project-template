#pragma once
// Host stub: station MAC for the provider test.
#include <stdint.h>
#define ESP_OK 0
#define WIFI_IF_STA 0
inline int esp_wifi_get_mac(int, uint8_t *mac) {
  const uint8_t m[6] = {0x02, 0x00, 0x5e, 0x10, 0x20, 0x30};
  for (int i = 0; i < 6; ++i) mac[i] = m[i];
  return ESP_OK;
}

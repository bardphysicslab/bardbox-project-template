#include "BardBoxNvsSettingsStore.h"

#if defined(ARDUINO) && defined(ESP_PLATFORM)
#include <Preferences.h>

namespace bbcore {

static const char *kNamespace = "bb-settings";
static const char *kKey = "s1";

SettingsStore::Load NvsSettingsStore::load(uint8_t *buf, size_t cap, size_t &len) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return Load::None;  // namespace absent: nothing saved
  const size_t stored = prefs.isKey(kKey) ? prefs.getBytesLength(kKey) : 0;
  Load result = Load::None;
  if (stored > cap) {
    result = Load::Failed;
  } else if (stored > 0) {
    len = prefs.getBytes(kKey, buf, cap);
    result = len == stored ? Load::Ok : Load::Failed;
  }
  prefs.end();
  return result;
}

bool NvsSettingsStore::save(const uint8_t *data, size_t len) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  const bool ok = prefs.putBytes(kKey, data, len) == len;
  prefs.end();
  return ok;
}

}  // namespace bbcore
#endif

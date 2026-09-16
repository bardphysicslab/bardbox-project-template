// Compile/link test only. No update installation or network activity.
#include <Arduino.h>
#include <Preferences.h> // Expose the built-in dependency to PlatformIO's scanner.
#include "BardBoxOTAVerifyESP32.h"
#include "BardBoxOTAStateStoreESP32.h"
// Keep the NVS adapter linked without performing any NVS operations in setup.
bool compileStateStore(bardbox::OTAStateStoreESP32 &store, bardbox::OTAState &state) {
    return store.begin() && store.load(state) && store.save(state);
}
auto volatile stateStoreCheck = &compileStateStore;
volatile bardbox::OTAVerification result;
auto volatile verifier = &bardbox::verifyOTAESP32;
void setup() {
    bardbox::OTAManifest manifest;
    bardbox::OTATarget target;
    result=verifier(manifest,target,"","","");
}
void loop() {}

// Compile/link test only. No update installation or network activity.
#include <Arduino.h>
#include <Preferences.h> // Expose the built-in dependency to PlatformIO's scanner.
#include "BardBoxOTAVerifyESP32.h"
#include "BardBoxOTAStateStoreESP32.h"
#include "BardBoxOTAAssignment.h"
#include "BardBoxOTAImageDigestESP32.h"
bool compileArtifactDigest(const esp_partition_t *partition,uint32_t bytes) {
    bardbox::OTAImageDigestESP32 digest;
    return digest.begin(partition,bytes) && digest.step()!=bardbox::OTAImageDigestESP32::Result::Failed;
}
auto volatile digestCheck = &compileArtifactDigest;
auto volatile assignmentParser = &bardbox::OTAAssignmentParser::parse;
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

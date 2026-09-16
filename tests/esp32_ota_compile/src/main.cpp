// Compile/link test only. No update installation or network activity.
#include <Arduino.h>
#include <Preferences.h> // Expose the built-in dependency to PlatformIO's scanner.
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "BardBoxOTAHTTPSESP32.h"
auto volatile reportCheck = &bardbox::OTAHTTPSESP32::report;
auto volatile pollCheck = &bardbox::OTAHTTPSESP32::poll;
#include "BardBoxOTAVerifyESP32.h"
#include "BardBoxOTAStateStoreESP32.h"
#include "BardBoxOTAAssignment.h"
#include "BardBoxOTAWriterESP32.h"
bool compileImageWriter(bardbox::OTAWriterESP32 &writer,bardbox::OTAAssignment &assignment,
                       bardbox::OTATarget &target,bardbox::OTAStateMachine &state,
                       bardbox::OTAStateStoreESP32 &store,const std::string &pem) {
    auto save=[&](const bardbox::OTAState &next){return store.save(next);};
    auto started=writer.begin(assignment,target,"lab",pem,state,std::string(64,'0'),1000,save);
    const uint8_t bytes[]={0};
    return started==bardbox::OTAWriterESP32::Start::Started && writer.append(bytes,1) && writer.finish(state,save);
}
auto volatile writerCheck = &compileImageWriter;
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
auto compileHTTPSInstall(const bardbox::OTAHTTPSConfig &config,const bardbox::OTAAssignment &assignment,
    const bardbox::OTATarget &target,const std::string &pem,bardbox::OTAStateMachine &state,
    bardbox::OTAStateStoreESP32 &store) -> bardbox::OTAHTTPSESP32::Install {
    auto save=[&](const bardbox::OTAState &next){return store.save(next);};
    return bardbox::OTAHTTPSESP32::install(config,assignment,target,"lab",pem,state,std::string(64,'0'),1000,save);
}
auto volatile httpsInstallCheck=&compileHTTPSInstall;
volatile bardbox::OTAVerification result;
auto volatile verifier = &bardbox::verifyOTAESP32;
void setup() {
    bardbox::OTAManifest manifest;
    bardbox::OTATarget target;
    result=verifier(manifest,target,"","","");
}
void loop() {}

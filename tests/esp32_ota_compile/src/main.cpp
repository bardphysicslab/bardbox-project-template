// Compile/link test only. No update installation or network activity.
#include <Arduino.h>
#include "BardBoxOTAVerifyESP32.h"
volatile bardbox::OTAVerification result;
auto volatile verifier = &bardbox::verifyOTAESP32;
void setup() {
    bardbox::OTAManifest manifest;
    bardbox::OTATarget target;
    result=verifier(manifest,target,"","","");
}
void loop() {}

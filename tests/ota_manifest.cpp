#include "BardBoxOTAManifest.h"
#include <cassert>
#include <iostream>
using namespace bardbox;
int main() {
    OTAManifest m;
    m.format="bardbox-ota-v1"; m.releaseId="bench-001"; m.component="firmware";
    m.version="0.7.0"; m.target="esp32-s3"; m.layout="dual-2m";
    m.configSchema="1"; m.queueSchema="bq1"; m.size=12345;
    m.sha256=std::string(64,'0'); m.keyId="lab-1";
    OTATarget target;
    target.component=m.component; target.target=m.target; target.layout=m.layout;
    target.configSchema=m.configSchema; target.queueSchema=m.queueSchema; target.slotBytes=2097152;
    std::string bytes;
    assert(canonicalOTA(m,bytes));
    std::cout<<bytes;
    assert(compatibleOTA(m,target));
    uint8_t hash[32]={};
    assert(matchesOTAImage(m,m.size,hash));
    hash[31]=1; assert(!matchesOTAImage(m,m.size,hash));
    hash[31]=0; assert(!matchesOTAImage(m,m.size-1,hash));
    assert(!matchesOTAImage(m,m.size,nullptr));
    for (auto field:{&OTAManifest::component,&OTAManifest::target,&OTAManifest::layout,
                     &OTAManifest::configSchema,&OTAManifest::queueSchema}) {
        OTAManifest changed=m; changed.*field="different";
        assert(!compatibleOTA(changed,target));
    }
    target.slotBytes=m.size-1; assert(!compatibleOTA(m,target));
    for (uint32_t size:{0U,2097153U,0xffffffffU}) {
        OTAManifest changed=m; changed.size=size;
        assert(!canonicalOTA(changed,bytes)); assert(bytes.empty());
    }
    for (auto field:{&OTAManifest::releaseId,&OTAManifest::component,&OTAManifest::version,
                     &OTAManifest::target,&OTAManifest::layout,&OTAManifest::configSchema,
                     &OTAManifest::queueSchema,&OTAManifest::keyId}) {
        for(const auto &bad:{std::string(),std::string(97,'x'),std::string("a\nb"),
                            std::string("bad space"),std::string("a\0b",3),std::string("é")}) {
            OTAManifest changed=m; changed.*field=bad;
            assert(!canonicalOTA(changed,bytes)); assert(bytes.empty());
        }
    }
    m.sha256[0]='A'; assert(!canonicalOTA(m,bytes));
}

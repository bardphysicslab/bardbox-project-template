#include "BardBoxOTAState.h"
#include "BardBoxOTAStateRecord.h"
#include <cassert>
using namespace bardbox;
int main() {
    OTAManifest m;
    m.format="bardbox-ota-v1"; m.releaseId="r1"; m.component="app"; m.version="0.7.0";
    m.target="esp32-s3"; m.layout="dual-2m"; m.configSchema="1"; m.queueSchema="bq1";
    m.sha256=std::string(64,'a'); m.keyId="lab"; m.size=1000;
    std::string oldHash(64,'b');
    OTAState durable;
    unsigned writes=0;
    auto save=[&](const OTAState &s) {durable=s; ++writes; return true;};
    auto reject=[](const OTAState &) {return false;};
    OTAStateMachine machine;
    assert(!machine.accept(m,1,oldHash,save)); // Missing/unprovisioned store.
    assert(machine.restore(durable)); // Explicit commissioning supplies idle.
    assert(!machine.accept(m,0,oldHash,save));
    assert(machine.accept(m,1,oldHash,save));
    assert(durable.phase==OTAPhase::Downloading && durable.generation==1);
    assert(machine.reserveStatus(save));
    assert(durable.sequence==1 && durable.phase==OTAPhase::Downloading);
    assert(!machine.accept(m,1,oldHash,save));
    assert(!machine.accept(m,2,oldHash,save)); // Cannot replace an active install.
    assert(!machine.confirm(save));
    assert(machine.downloadedAndVerified(save));
    assert(durable.phase==OTAPhase::PendingBoot);
    assert(machine.restore(durable));
    assert(machine.booted(m.sha256,save));
    assert(durable.phase==OTAPhase::Validating);
    assert(machine.confirm(save));
    assert(durable.phase==OTAPhase::Confirmed);
    auto sequence=durable.sequence;
    assert(machine.booted(m.sha256,save));
    assert(durable.sequence==sequence+1);
    assert(!machine.accept(m,1,oldHash,save));
    assert(machine.accept(m,2,m.sha256,save)); // Same bytes, no download/reboot.
    assert(durable.phase==OTAPhase::Confirmed && durable.sequence==0);

    for(bool commitBeforeFailure:{false,true}) {
        durable=OTAState(); assert(machine.restore(durable));
        auto uncertain=[&](const OTAState &s) {if(commitBeforeFailure) durable=s;return false;};
        assert(!machine.accept(m,1,oldHash,uncertain));
        assert(!machine.ready());
        assert(!machine.accept(m,1,oldHash,save)); // Cannot act after uncertain write.
        assert(machine.restore(durable));
        assert(machine.booted(oldHash,save));
        if(commitBeforeFailure) {
            assert(durable.phase==OTAPhase::Failed);
            assert(!machine.accept(m,1,oldHash,save));
            assert(machine.accept(m,2,oldHash,save));
        } else assert(machine.accept(m,1,oldHash,save));
    }
    assert(!machine.downloadedAndVerified(reject));
    assert(!machine.ready());
    assert(machine.restore(durable));
    assert(machine.booted(oldHash,save));
    assert(durable.phase==OTAPhase::Failed);
    assert(!machine.accept(m,1,oldHash,save));

    assert(machine.accept(m,3,oldHash,save));
    assert(machine.downloadedAndVerified(save));
    assert(machine.restore(durable));
    assert(machine.booted(oldHash,save));
    assert(durable.phase==OTAPhase::RolledBack);
    assert(!machine.accept(m,3,oldHash,save));
    assert(!machine.fail("rewrite-terminal",save));
    auto bad=durable; bad.candidateHash="bad";
    assert(!machine.restore(bad));
    bad=durable; bad.phase=static_cast<OTAPhase>(255);
    assert(!machine.restore(bad));
    bad=durable; bad.sequence=UINT32_MAX;
    assert(machine.restore(bad));
    assert(!machine.booted(oldHash,save));
    assert(!machine.ready());
    assert(writes>0);
    std::string record;
    assert(OTAStateRecord::encode(durable,record));
    OTAState decoded;
    assert(OTAStateRecord::decode(record,decoded));
    assert(decoded.generation==durable.generation && decoded.sequence==durable.sequence &&
           decoded.phase==durable.phase && decoded.releaseId==durable.releaseId &&
           decoded.candidateHash==durable.candidateHash && decoded.previousHash==durable.previousHash &&
           decoded.version==durable.version && decoded.failure==durable.failure);
    for(size_t i=0;i<record.size();++i) {
        OTAState untouched=decoded;
        assert(!OTAStateRecord::decode(record.substr(0,i),untouched));
        assert(untouched.generation==decoded.generation);
        std::string corrupt=record; corrupt[i]^=1;
        assert(!OTAStateRecord::decode(corrupt,untouched));
    }
    assert(!OTAStateRecord::decode(record+"x",decoded));
    assert(!OTAStateRecord::decode(std::string(513,'x'),decoded));
    assert(OTAStateRecord::encode(OTAState(),record));
    assert(OTAStateRecord::decode(record,decoded) && decoded.phase==OTAPhase::Idle);
}

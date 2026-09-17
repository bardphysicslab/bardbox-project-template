#include "BardBoxOTAStatus.h"
#include <cassert>
#include <iostream>
using namespace bardbox;
int main() {
    OTAState s;
    s.generation=1; s.sequence=2147483647; s.phase=OTAPhase::Confirmed;
    s.candidateBytes=19; s.previousBytes=19; s.releaseId="release-1";
    s.candidateHash=std::string(64,'a'); s.previousHash=std::string(64,'b');
    s.version="0.7.0"; s.failure="none";
    OTAStateMachine machine;
    assert(machine.restore(s));
    OTAStatusEvent event;
    unsigned writes=0;
    auto save=[&](const OTAState &next){++writes;s=next;return true;};
    assert(!event.prepare(machine,"bad\"boot",s.version,s.candidateHash,19,save));
    assert(!event.prepare(machine,"boot-1",s.version,s.previousHash,19,save));
    assert(!event.prepare(machine,"boot-1",s.version,s.candidateHash,20,save));
    assert(writes==0 && event.body().empty());
    assert(event.prepare(machine,"boot-1",s.version,s.candidateHash,19,save));
    assert(s.sequence==2147483648U && writes==1);
    auto immutable=event.body();
    assert(event.body()==immutable && writes==1); // retries do not reserve again
    std::cout << event.body() << '\n';
    for(auto phase:{OTAPhase::Downloading,OTAPhase::PendingBoot,OTAPhase::Validating,
                    OTAPhase::Failed,OTAPhase::RolledBack}) {
        s.phase=phase; assert(machine.restore(s));
        assert(event.prepare(machine,"boot-2","0.6.0",s.previousHash,0,save));
    }
    // Ambiguous durable save cannot emit an event, even if the store did commit.
    assert(!event.prepare(machine,"boot-3","0.6.0",s.previousHash,0,
        [&](const OTAState &next){s=next;return false;}));
    assert(event.body().empty() && !machine.ready());
    assert(machine.restore(s));
    auto reserved=s.sequence;
    assert(event.prepare(machine,"boot-4","0.6.0",s.previousHash,0,save));
    assert(s.sequence==reserved+1);
    s.sequence=UINT32_MAX; assert(machine.restore(s));
    assert(!event.prepare(machine,"boot-4","0.6.0",s.previousHash,0,save));
    assert(event.body().empty() && !machine.ready());
}

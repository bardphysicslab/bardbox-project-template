#pragma once
#include "BardBoxOTAState.h"

namespace bardbox {
// One immutable event per successful preparation. Retry this exact body after a
// lost response. Caller serializes access and discards pending events on reboot;
// the next preparation reserves a fresh durable sequence before emission.
class OTAStatusEvent {
public:
    const std::string &body() const { return body_; }
    template<class Persist>
    bool prepare(OTAStateMachine &machine, const std::string &bootId,
                 const std::string &runningVersion, const std::string &runningHash,
                 uint32_t progress, Persist persist) {
        body_.clear();
        const auto &s=machine.state();
        if(!machine.ready() || !OTAStateMachine::valid(s) || s.phase==OTAPhase::Idle ||
           !otaToken(bootId) || !otaToken(runningVersion) ||
           !OTAStateMachine::digest(runningHash) || progress>s.candidateBytes) return false;
        if(s.phase==OTAPhase::Confirmed &&
           (runningVersion!=s.version || runningHash!=s.candidateHash)) return false;
        const char *phase=nullptr;
        switch(s.phase) {
            case OTAPhase::Downloading: phase="downloading"; break;
            case OTAPhase::PendingBoot: phase="pending_reboot"; break;
            case OTAPhase::Validating: phase="validating"; break;
            case OTAPhase::Confirmed: phase="confirmed"; break;
            case OTAPhase::Failed: phase="failed"; break;
            case OTAPhase::RolledBack: phase="rolled_back"; break;
            default: return false;
        }
        if(!machine.reserveStatus(persist)) return false;
        // All strings are restricted ASCII tokens or lower-case SHA256. No
        // escaping, credentials, raw errors or unbounded payloads enter JSON.
        const auto &saved=machine.state();
        body_="{\"generation\":"+std::to_string(saved.generation)+
              ",\"sequence\":"+std::to_string(saved.sequence)+
              ",\"boot_id\":\""+bootId+"\",\"state\":\""+phase+
              "\",\"running_version\":\""+runningVersion+
              "\",\"running_sha256\":\""+runningHash+
              "\",\"bytes\":"+std::to_string(progress)+
              ",\"failure\":\""+saved.failure+"\"}";
        return true;
    }
private:
    std::string body_;
};
} // namespace bardbox

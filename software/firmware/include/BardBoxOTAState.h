#pragma once
#include "BardBoxOTAManifest.h"

namespace bardbox {
enum class OTAPhase : uint8_t { Idle, Downloading, PendingBoot, Validating,
    Confirmed, Failed, RolledBack };
struct OTAState {
    uint32_t generation=0, sequence=0;
    uint32_t candidateBytes=0, previousBytes=0;
    OTAPhase phase=OTAPhase::Idle;
    std::string releaseId, candidateHash, previousHash, version, failure;
};

// Policy only. Persist must atomically store the complete state before returning
// true. False (including uncertain outcome) disables further actions until reload.
// A missing/corrupt store is not equivalent to a provisioned idle state.
class OTAStateMachine {
public:
    static bool digest(const std::string &s) {
        if (s.size()!=64) return false;
        for(char c:s) if(!((c>='0' && c<='9') || (c>='a' && c<='f'))) return false;
        return true;
    }
    static bool valid(const OTAState &s) {
        if (s.phase==OTAPhase::Idle)
            return s.generation==0 && s.sequence==0 && !s.candidateBytes && !s.previousBytes && s.releaseId.empty() &&
                s.candidateHash.empty() && s.previousHash.empty() && s.version.empty() && s.failure.empty();
        return s.generation>0 && s.candidateBytes>0 && s.candidateBytes<=2097152 &&
            s.previousBytes>0 && s.previousBytes<=2097152 &&
            static_cast<unsigned>(s.phase)<=static_cast<unsigned>(OTAPhase::RolledBack) &&
            otaToken(s.releaseId) && digest(s.candidateHash) && digest(s.previousHash) &&
            otaToken(s.version) && otaToken(s.failure);
    }
    bool restore(const OTAState &s) {
        ready_=valid(s);
        if(ready_) state_=s;
        return ready_;
    }
    bool ready() const { return ready_; }
    const OTAState &state() const { return state_; }
    template<class Persist>
    bool reserveStatus(Persist persist) {
        if(!ready_ || state_.phase==OTAPhase::Idle) return false;
        return change(state_.phase,state_.failure,persist);
    }

    // Caller verifies signature and compatibility before calling accept.
    // Old/equal generations never restart an installation. A same-image new
    // assignment is acknowledged without rebooting or touching flash.
    template<class Persist>
    bool accept(const OTAManifest &m, uint32_t generation, const std::string &runningHash,
                uint32_t runningBytes, Persist persist) {
        std::string bytes;
        if(!ready_ || !canonicalOTA(m,bytes) || !digest(runningHash) || !runningBytes || runningBytes>2097152 ||
           (runningHash==m.sha256 && runningBytes!=m.size) || generation<=state_.generation ||
           (state_.phase!=OTAPhase::Idle && !terminal(state_.phase))) return false;
        OTAState next;
        next.generation=generation; next.releaseId=m.releaseId; next.version=m.version;
        next.candidateHash=m.sha256; next.previousHash=runningHash; next.failure="none";
        next.candidateBytes=m.size; next.previousBytes=runningBytes;
        next.phase=runningHash==m.sha256 ? OTAPhase::Confirmed : OTAPhase::Downloading;
        return commit(next,persist);
    }
    template<class Persist>
    bool downloadedAndVerified(Persist persist) {
        if(!ready_ || state_.phase!=OTAPhase::Downloading) return false;
        return change(OTAPhase::PendingBoot,"none",persist);
    }
    template<class Persist>
    bool booted(const std::string &runningHash, uint32_t runningBytes, Persist persist) {
        if(!ready_ || !digest(runningHash) || !runningBytes || runningBytes>2097152) return false;
        if(state_.phase==OTAPhase::Idle) return true;
        if(state_.phase==OTAPhase::Downloading)
            return change(OTAPhase::Failed,"interrupted_download",persist);
        if(state_.phase==OTAPhase::PendingBoot || state_.phase==OTAPhase::Validating) {
            if(runningHash==state_.candidateHash && runningBytes==state_.candidateBytes) return change(OTAPhase::Validating,"none",persist);
            if(runningHash==state_.previousHash && runningBytes==state_.previousBytes) return change(OTAPhase::RolledBack,"previous_image_running",persist);
            return change(OTAPhase::Failed,"unexpected_running_image",persist);
        }
        // Reserve a new event sequence for this boot even when the outcome is
        // unchanged; a new boot ID must not reuse an earlier event's sequence.
        if(state_.phase==OTAPhase::Confirmed && (runningHash!=state_.candidateHash || runningBytes!=state_.candidateBytes)) {
            ready_=false; // Separate device-health fault, not a rewritten terminal assignment.
            return false;
        }
        return change(state_.phase,state_.failure,persist);
    }
    template<class Persist>
    bool confirm(Persist persist) {
        if(!ready_ || state_.phase!=OTAPhase::Validating) return false;
        return change(OTAPhase::Confirmed,"none",persist);
    }
    template<class Persist>
    bool fail(const std::string &reason, Persist persist) {
        if(!ready_ || state_.phase==OTAPhase::Idle || terminal(state_.phase) || !otaToken(reason)) return false;
        return change(OTAPhase::Failed,reason,persist);
    }
private:
    static bool terminal(OTAPhase p) {
        return p==OTAPhase::Confirmed || p==OTAPhase::Failed || p==OTAPhase::RolledBack;
    }
    template<class Persist>
    bool change(OTAPhase phase, const std::string &failure, Persist persist) {
        if(state_.sequence==UINT32_MAX) { ready_=false; return false; }
        OTAState next=state_;
        ++next.sequence; next.phase=phase; next.failure=failure;
        return commit(next,persist);
    }
    template<class Persist>
    bool commit(const OTAState &next, Persist persist) {
        if(!valid(next) || !persist(next)) { ready_=false; return false; }
        state_=next;
        return true;
    }
    bool ready_=false;
    OTAState state_;
};
} // namespace bardbox

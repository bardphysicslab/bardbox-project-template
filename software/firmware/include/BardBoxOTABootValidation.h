#pragma once
#include <stdint.h>
namespace bardbox {
// Local-only acceptance policy. Network connectivity and individual sensor
// health do not decide whether a new application can retain its boot slot.
class OTABootValidation {
public:
    enum class Result { Waiting, Confirm, Rollback };
    void begin(uint32_t now,uint32_t persisted,uint32_t budgetMs=150000) {
        start_=now; baseline_=persisted; budget_=budgetMs; started_=true;
    }
    Result observe(uint32_t now,uint32_t persisted,uint32_t lastAcquisitionMs,
                   bool configOK,bool storageOK) const {
        if(!started_ || !budget_ || budget_>600000 || !configOK || !storageOK) return Result::Rollback;
        if(static_cast<uint32_t>(now-start_)>=budget_) return Result::Rollback;
        if(persisted!=baseline_ && static_cast<uint32_t>(now-lastAcquisitionMs)<=5000) return Result::Confirm;
        return Result::Waiting;
    }
private:
    uint32_t start_=0,baseline_=0,budget_=0;
    bool started_=false;
};
} // namespace bardbox

#pragma once
#include "BardBoxOTAStateRecord.h"
#include <Preferences.h>

namespace bardbox {
// One NVS blob per transition. Namespace is separate from credentials and queue.
// Caller serializes access; never erase NVS as recovery for load/save failure.
class OTAStateStoreESP32 {
public:
    bool begin() { return preferences_.begin("bb-ota",false); }
    bool load(OTAState &state) {
        std::string bytes;
        return read(bytes) && OTAStateRecord::decode(bytes,state);
    }
    bool save(const OTAState &state) {
        OTAState current;
        // Normal operation cannot silently reinitialize missing/corrupt state.
        if(!load(current) || state.generation<current.generation ||
           (state.generation==current.generation && state.sequence<current.sequence)) return false;
        return write(state);
    }
    // Only deliberate first commissioning may create idle state. Never called
    // automatically by the updater when load() fails.
    bool provisionIdle() {
        if(preferences_.isKey("state")) return false;
        return write(OTAState());
    }
private:
    bool read(std::string &bytes) {
        size_t size=preferences_.getBytesLength("state");
        if(!size || size>OTAStateRecord::MaxBytes) return false;
        bytes.assign(size,'\0');
        return preferences_.getBytes("state",&bytes[0],size)==size;
    }
    bool write(const OTAState &state) {
        std::string bytes, check;
        if(!OTAStateRecord::encode(state,bytes)) return false;
        if(preferences_.putBytes("state",bytes.data(),bytes.size())!=bytes.size()) return false;
        return read(check) && check==bytes;
    }
    Preferences preferences_;
};
} // namespace bardbox

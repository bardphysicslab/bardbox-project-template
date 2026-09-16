#pragma once
#include "BardBoxOTAAssignment.h"
#include "BardBoxOTAState.h"
#include "BardBoxOTAVerifyESP32.h"
#include <esp_ota_ops.h>

namespace bardbox {
// Caller owns a single OTA worker, verified HTTPS delivery, deadlines, status
// reporting and sampling-safe reboot scheduling. This class never restarts.
class OTAWriterESP32 {
public:
    enum class Start { Started, AlreadyCurrent, Rejected };
    OTAWriterESP32() { mbedtls_sha256_init(&hash_); }
    ~OTAWriterESP32() { abort(); mbedtls_sha256_free(&hash_); }
    OTAWriterESP32(const OTAWriterESP32 &)=delete;
    OTAWriterESP32 &operator=(const OTAWriterESP32 &)=delete;

    template<class Persist>
    Start begin(const OTAAssignment &assignment, const OTATarget &target,
                const std::string &keyId, const std::string &publicPem,
                OTAStateMachine &state, const std::string &runningHash,
                uint32_t runningBytes, Persist persist) {
        if(active_) return Start::Rejected;
        readyForReboot_=false;
        const auto &m=assignment.manifest;
        if(verifyOTAESP32(m,target,assignment.signature,keyId,publicPem)!=OTAVerification::Valid)
            return Start::Rejected;
        const esp_partition_t *running=esp_ota_get_running_partition();
        const esp_partition_t *next=esp_ota_get_next_update_partition(nullptr);
        if(!running || !next || running->address==next->address ||
           next->type!=ESP_PARTITION_TYPE_APP || next->size!=target.slotBytes ||
           m.size>next->size || runningBytes>running->size) return Start::Rejected;
        if(!state.accept(m,assignment.generation,runningHash,runningBytes,persist)) return Start::Rejected;
        if(state.state().phase==OTAPhase::Confirmed) return Start::AlreadyCurrent;
        manifest_=m; generation_=assignment.generation; partition_=next; received_=0;
        if(mbedtls_sha256_starts_ret(&hash_,0)!=0 || esp_ota_begin(next,m.size,&handle_)!=ESP_OK) {
            state.fail("inactive_slot_begin_failed",persist);
            return Start::Rejected;
        }
        active_=true;
        return Start::Started;
    }
    bool append(const uint8_t *bytes,size_t count) {
        if(!active_) return false;
        if(!bytes || !count || count>1024 || count>manifest_.size-received_ ||
           mbedtls_sha256_update_ret(&hash_,bytes,count)!=0 ||
           esp_ota_write(handle_,bytes,count)!=ESP_OK) { abort(); return false; }
        received_+=count;
        return true;
    }
    template<class Persist>
    bool finish(OTAStateMachine &state,Persist persist,bool selectBoot=true) {
        if(!active_) return false;
        const auto &saved=state.state();
        if(!state.ready() || saved.phase!=OTAPhase::Downloading || saved.generation!=generation_ ||
           saved.candidateHash!=manifest_.sha256 || saved.candidateBytes!=manifest_.size) {
            abort(); return false;
        }
        uint8_t digest[32];
        if(received_!=manifest_.size || mbedtls_sha256_finish_ret(&hash_,digest)!=0 ||
           !matchesOTAImage(manifest_,received_,digest)) {
            abort(); state.fail("download_digest_or_size_failed",persist); return false;
        }
        // esp_ota_end validates the ESP image and frees its handle on all outcomes.
        esp_err_t ended=esp_ota_end(handle_);
        active_=false;
        if(ended!=ESP_OK) { state.fail("platform_image_invalid",persist); return false; }
        if(!state.downloadedAndVerified(persist)) return false;
        // A project can defer selection until its acquisition boundary is safe.
        if(!selectBoot) return true;
        if(esp_ota_set_boot_partition(partition_)!=ESP_OK) {
            state.fail("boot_selection_failed",persist); return false;
        }
        readyForReboot_=true;
        return true;
    }
    void abort() {
        if(active_) esp_ota_abort(handle_);
        active_=false;
    }
    uint32_t bytesWritten() const { return received_; }
    bool readyForReboot() const { return readyForReboot_; }
private:
    mbedtls_sha256_context hash_;
    esp_ota_handle_t handle_=0;
    const esp_partition_t *partition_=nullptr;
    OTAManifest manifest_;
    uint32_t generation_=0,received_=0;
    bool active_=false,readyForReboot_=false;
};
} // namespace bardbox

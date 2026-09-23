#pragma once
#include <esp_partition.h>
#include <mbedtls/sha256.h>
#include <string>

namespace bardbox {
// Hash the exact signed artifact extent, including any appended ESP image hash.
// esp_partition_get_sha256() may instead return that appended hash, which is a
// different digest. Each step reads at most 1024 bytes; caller schedules/yields.
class OTAImageDigestESP32 {
public:
    enum class Result { Reading, Complete, Failed };
    OTAImageDigestESP32() { mbedtls_sha256_init(&hash_); }
    ~OTAImageDigestESP32() { mbedtls_sha256_free(&hash_); }
    OTAImageDigestESP32(const OTAImageDigestESP32 &)=delete;
    OTAImageDigestESP32 &operator=(const OTAImageDigestESP32 &)=delete;
    bool begin(const esp_partition_t *partition,uint32_t artifactBytes) {
        status_=Result::Failed; digest_.clear(); position_=0;
        if(!partition || partition->type!=ESP_PARTITION_TYPE_APP || !artifactBytes ||
           artifactBytes>2097152 || artifactBytes>partition->size) return false;
        if(mbedtls_sha256_starts_ret(&hash_,0)!=0) return false;
        partition_=partition; length_=artifactBytes; status_=Result::Reading;
        return true;
    }
    Result step() {
        if(status_!=Result::Reading) return status_;
        unsigned char bytes[1024];
        size_t size=length_-position_;
        if(size>sizeof(bytes)) size=sizeof(bytes);
        if(esp_partition_read(partition_,position_,bytes,size)!=ESP_OK ||
           mbedtls_sha256_update_ret(&hash_,bytes,size)!=0) return status_=Result::Failed;
        position_+=size;
        if(position_<length_) return status_;
        unsigned char result[32];
        if(mbedtls_sha256_finish_ret(&hash_,result)!=0) return status_=Result::Failed;
        static const char hex[]="0123456789abcdef";
        for(unsigned char value:result) { digest_+=hex[value>>4]; digest_+=hex[value&15]; }
        return status_=Result::Complete;
    }
    const std::string &digest() const { return digest_; }
    uint32_t bytesRead() const { return position_; }
private:
    mbedtls_sha256_context hash_;
    const esp_partition_t *partition_=nullptr;
    uint32_t length_=0,position_=0;
    Result status_=Result::Failed;
    std::string digest_;
};
} // namespace bardbox

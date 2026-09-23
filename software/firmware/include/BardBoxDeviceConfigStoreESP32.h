#pragma once
#include "BardBoxDeviceConfig.h"
#include <Preferences.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>

namespace bardbox {
class DeviceConfigStoreESP32 {
public:
    bool begin() { return preferences_.begin("bb-device",false); }
    bool load(DeviceConfig &config) {
        std::string bytes;
        return read(bytes) && DeviceConfigRecord::decode(bytes,config) && trustedMaterial(config);
    }
    // Explicit USB commissioning only. A different/corrupt existing record is
    // never overwritten. Identical retries permit recovery after lost ACK/power.
    bool provision(const DeviceConfig &config) {
        std::string bytes,existing;
        if(!DeviceConfigRecord::encode(config,bytes) || !trustedMaterial(config)) return false;
        if(preferences_.isKey("config")) return read(existing) && existing==bytes;
        if(preferences_.putBytes("config",bytes.data(),bytes.size())!=bytes.size()) return false;
        return read(existing) && existing==bytes;
    }
    bool exists() { return preferences_.isKey("config"); }
    static bool trustedMaterial(const DeviceConfig &config) {
        if(!config.valid()) return false;
        mbedtls_pk_context key; mbedtls_pk_init(&key);
        int result=mbedtls_pk_parse_public_key(&key,
            reinterpret_cast<const unsigned char *>(config.signingPublicPem.c_str()),config.signingPublicPem.size()+1);
        bool keyOK=result==0 && mbedtls_pk_can_do(&key,MBEDTLS_PK_ECDSA) &&
            mbedtls_pk_ec(key)->grp.id==MBEDTLS_ECP_DP_SECP256R1;
        mbedtls_pk_free(&key);
        mbedtls_x509_crt ca; mbedtls_x509_crt_init(&ca);
        result=mbedtls_x509_crt_parse(&ca,reinterpret_cast<const unsigned char *>(config.ota.caPem.c_str()),
            config.ota.caPem.size()+1);
        bool caOK=result==0 && ca.raw.len>0;
        mbedtls_x509_crt_free(&ca);
        return config.valid() && keyOK && caOK;
    }
private:
    bool read(std::string &bytes) {
        size_t n=preferences_.getBytesLength("config");
        if(!n || n>DeviceConfigRecord::MaxBytes) return false;
        bytes.assign(n,'\0'); return preferences_.getBytes("config",&bytes[0],n)==n;
    }
    Preferences preferences_;
};
} // namespace bardbox

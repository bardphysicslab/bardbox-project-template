#pragma once
#include "BardBoxOTAManifest.h"
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

namespace bardbox {
enum class OTAVerification { Valid, InvalidManifest, Incompatible, UntrustedKey,
    InvalidSignatureEncoding, InvalidKey, InvalidSignature };

// ESP32 Arduino 2.x / mbedTLS 2.x adapter. Public key comes only from provisioned
// trust configuration, never from a downloaded envelope. No flash/network writes.
inline OTAVerification verifyOTAESP32(const OTAManifest &m, const OTATarget &target,
        const std::string &signatureBase64, const std::string &trustedKeyId,
        const std::string &trustedPublicPem) {
    std::string message;
    if (!canonicalOTA(m,message)) return OTAVerification::InvalidManifest;
    if (!compatibleOTA(m,target)) return OTAVerification::Incompatible;
    if (m.keyId!=trustedKeyId) return OTAVerification::UntrustedKey;
    if (trustedPublicPem.empty() || trustedPublicPem.size()>2048 ||
        trustedPublicPem.find('\0')!=std::string::npos) return OTAVerification::InvalidKey;
    if (signatureBase64.empty() || signatureBase64.size()>104 || signatureBase64.size()%4)
        return OTAVerification::InvalidSignatureEncoding;
    for (char c:signatureBase64)
        if (!((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') ||
            c=='+' || c=='/' || c=='=')) return OTAVerification::InvalidSignatureEncoding;
    unsigned char signature[80], digest[32];
    size_t length=0;
    if (mbedtls_base64_decode(signature,sizeof(signature),&length,
            reinterpret_cast<const unsigned char *>(signatureBase64.data()),signatureBase64.size())!=0)
        return OTAVerification::InvalidSignatureEncoding;
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    int parsed=mbedtls_pk_parse_public_key(&key,
        reinterpret_cast<const unsigned char *>(trustedPublicPem.c_str()),trustedPublicPem.size()+1);
    bool correctKey=parsed==0 && mbedtls_pk_can_do(&key,MBEDTLS_PK_ECDSA) &&
        mbedtls_pk_ec(key)->grp.id==MBEDTLS_ECP_DP_SECP256R1;
    if (!correctKey) { mbedtls_pk_free(&key); return OTAVerification::InvalidKey; }
    int hashed=mbedtls_sha256_ret(reinterpret_cast<const unsigned char *>(message.data()),
                                message.size(),digest,0);
    int verified=hashed ? hashed : mbedtls_pk_verify(&key,MBEDTLS_MD_SHA256,digest,
                                                   sizeof(digest),signature,length);
    mbedtls_pk_free(&key);
    return verified==0 ? OTAVerification::Valid : OTAVerification::InvalidSignature;
}
} // namespace bardbox

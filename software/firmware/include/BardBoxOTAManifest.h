#pragma once
#include <stdint.h>
#include <string>

namespace bardbox {
struct OTAManifest {
    std::string format, releaseId, component, version, target, layout;
    std::string configSchema, queueSchema, sha256, keyId;
    uint32_t size=0;
};

struct OTATarget {
    std::string component, target, layout, configSchema, queueSchema;
    uint32_t slotBytes=0;
};

inline bool otaToken(const std::string &s) {
    if (s.empty() || s.size()>96) return false;
    for (unsigned char c:s)
        if (!((c>='A' && c<='Z') || (c>='a' && c<='z') ||
              (c>='0' && c<='9') || c=='_' || c=='.' || c=='-')) return false;
    return true;
}

// Exact ASCII/LF contract used by the Python signing tool. The JSON decoder must
// separately reject missing/extra fields, duplicate keys and non-integer sizes.
inline bool canonicalOTA(const OTAManifest &m, std::string &bytes) {
    bytes.clear();
    if (m.releaseId=="." || m.releaseId=="..") return false;
    if (m.format!="bardbox-ota-v1" || !m.size || m.size>2U*1024U*1024U || m.sha256.size()!=64)
        return false;
    for (char c:m.sha256) if (!((c>='a' && c<='f') || (c>='0' && c<='9'))) return false;
    const std::string *fields[]={&m.format,&m.releaseId,&m.component,&m.version,&m.target,
        &m.layout,&m.configSchema,&m.queueSchema};
    for (const auto *field:fields) if (!otaToken(*field)) return false;
    if (!otaToken(m.keyId)) return false;
    for (const auto *field:fields) bytes+=*field+'\n';
    bytes+=std::to_string(m.size)+'\n'+m.sha256+'\n'+m.keyId+'\n';
    return true;
}

inline bool compatibleOTA(const OTAManifest &m, const OTATarget &t) {
    std::string bytes;
    return canonicalOTA(m,bytes) && m.component==t.component && m.target==t.target &&
        m.layout==t.layout && m.configSchema==t.configSchema && m.queueSchema==t.queueSchema &&
        m.size<=t.slotBytes;
}

// Compare a completed streaming SHA-256 and byte count before selecting a slot.
// This does not authenticate a manifest: signature verification is also required.
inline bool matchesOTAImage(const OTAManifest &m, uint32_t size, const uint8_t digest[32]) {
    std::string bytes;
    if (!digest || !canonicalOTA(m,bytes) || size!=m.size) return false;
    static const char hex[]="0123456789abcdef";
    unsigned mismatch=0;
    for (size_t i=0;i<32;++i) {
        mismatch |= static_cast<unsigned>(m.sha256[2*i]^hex[digest[i]>>4]);
        mismatch |= static_cast<unsigned>(m.sha256[2*i+1]^hex[digest[i]&15]);
    }
    return mismatch==0;
}
} // namespace bardbox

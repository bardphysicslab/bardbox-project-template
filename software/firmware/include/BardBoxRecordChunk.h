#pragma once
#include "BardBoxRecordDelta.h"
#include <memory>

namespace bardbox {
// Storage framing only. The caller owns file flush/close, atomic ACK metadata,
// and deletion. Readers never erase or repair a damaged file. Allocate on heap.
class RecordChunk {
public:
    static const size_t MaxRecords = 50;
    static const size_t MaxPayload = RecordDelta::MaxRecord * 2 + 13;
    enum class Result { Record, End, Incomplete, Corrupt, WrongChunk, Full };

    static std::string header(uint32_t sequence) {
        std::string out("BQ1\0", 4);
        put32(out, sequence);
        put32(out, RecordDelta::crc32(out));
        return out;
    }

    static bool frame(const std::string &payload, std::string &out) {
        out.clear();
        if (payload.size() > MaxPayload) return false;
        out.push_back(static_cast<char>(payload.size() & 255));
        out.push_back(static_cast<char>(payload.size() >> 8));
        out += payload;
        put32(out, RecordDelta::crc32(out));
        return true;
    }

    explicit RecordChunk(uint32_t sequence) : sequence_(sequence) {}
    size_t validBytes() const { return offset_; }
    size_t records() const { return count_; }

    // read(offset, destination, count) returns bytes read. fileSize is a stable
    // snapshot under the queue lock. No allocation proportional to file size.
    // Failure leaves the cursor unchanged; seal the file and start a new chunk.
    template<class Read>
    Result next(Read read, size_t fileSize, std::string &record) {
        record.clear();
        if (!opened_) {
            if (fileSize < 12) return Result::Incomplete;
            char raw[12];
            if (read(0, raw, 12) != 12) return Result::Incomplete;
            std::string h(raw, 12);
            if (h.compare(0, 4, std::string("BQ1\0", 4)) != 0 ||
                get32(h, 8) != RecordDelta::crc32(h.substr(0, 8))) return Result::Corrupt;
            if (get32(h, 4) != sequence_) return Result::WrongChunk;
            opened_ = true;
            offset_ = 12;
        }
        if (fileSize < offset_) return Result::Incomplete;
        if (fileSize == offset_) return Result::End;
        if (count_ == MaxRecords) return Result::Full;
        if (fileSize - offset_ < 2) return Result::Incomplete;
        char lengthBytes[2];
        if (read(offset_, lengthBytes, 2) != 2) return Result::Incomplete;
        size_t length = static_cast<unsigned char>(lengthBytes[0]) |
                        (static_cast<size_t>(static_cast<unsigned char>(lengthBytes[1])) << 8);
        if (length > MaxPayload || (!count_ && length > RecordDelta::MaxRecord)) return Result::Corrupt;
        if (fileSize - offset_ < length + 6) return Result::Incomplete;
        std::string encoded(length + 6, '\0');
        if (read(offset_, &encoded[0], encoded.size()) != encoded.size()) return Result::Incomplete;
        if (get32(encoded, length + 2) != RecordDelta::crc32(encoded.substr(0, length + 2)))
            return Result::Corrupt;
        const std::string payload = encoded.substr(2, length);
        if (!count_) {
            base_.reset(new RecordDelta(payload));
            record = payload;
        } else if (!base_->decode(payload, record)) return Result::Corrupt;
        offset_ += length + 6;
        ++count_;
        return Result::Record;
    }

private:
    static void put32(std::string &s, uint32_t value) {
        for (unsigned i=0; i<4; ++i) s.push_back(static_cast<char>(value >> (8*i)));
    }
    static uint32_t get32(const std::string &s, size_t offset) {
        uint32_t value=0;
        for (unsigned i=0; i<4; ++i)
            value |= static_cast<uint32_t>(static_cast<unsigned char>(s[offset+i])) << (8*i);
        return value;
    }
    uint32_t sequence_;
    size_t offset_=0, count_=0;
    bool opened_=false;
    std::unique_ptr<RecordDelta> base_;
};
} // namespace bardbox

#include "BardBoxRecordChunk.h"
#include <cassert>
#include <cstring>
using bardbox::RecordChunk;
using bardbox::RecordDelta;
using Result = RecordChunk::Result;

struct Reader {
    const std::string &bytes;
    size_t operator()(size_t offset, char *out, size_t count) const {
        assert(offset <= bytes.size());
        count = std::min(count, bytes.size()-offset);
        memcpy(out, bytes.data()+offset, count);
        return count;
    }
};

int main() {
    std::vector<std::string> records;
    std::vector<size_t> ends;
    const std::string base="{\"uid\":\"node-one\",\"pm\":12.34,\"window\":1}";
    RecordDelta codec(base);
    std::string bytes=RecordChunk::header(42), frame, encoded, out;
    for (size_t i=0;i<50;++i) {
        records.push_back(i ? "{\"uid\":\"node-one\",\"pm\":45.67,\"window\":"+std::to_string(i+1)+"}" : base);
        if (!i) encoded=base;
        else assert(codec.encode(records.back(), encoded));
        assert(RecordChunk::frame(encoded, frame));
        bytes+=frame;
        ends.push_back(bytes.size());
    }
    // Every possible power-loss position preserves exactly the complete prefix.
    for (size_t cut=0;cut<=bytes.size();++cut) {
        std::string truncated=bytes.substr(0,cut);
        Reader reader{truncated};
        RecordChunk cursor(42);
        size_t n=0;
        Result result;
        while ((result=cursor.next(reader,truncated.size(),out))==Result::Record) {
            assert(out==records[n++]);
        }
        size_t expected=std::upper_bound(ends.begin(),ends.end(),cut)-ends.begin();
        assert(n==expected);
        assert(cursor.validBytes()==(n ? ends[n-1] : cut>=12 ? 12 : 0));
        assert(result==Result::End || result==Result::Incomplete);
        assert(out.empty());
    }
    // One-bit corruption at every byte must never produce a changed record.
    for (size_t at=0;at<bytes.size();++at) {
        std::string damaged=bytes;
        damaged[at]^=1;
        Reader reader{damaged};
        RecordChunk cursor(42);
        size_t n=0;
        Result result;
        while ((result=cursor.next(reader,damaged.size(),out))==Result::Record)
            assert(out==records[n++]);
        assert(n<records.size());
        assert(result==Result::Corrupt || result==Result::Incomplete);
        assert(out.empty());
    }
    Reader reader{bytes};
    RecordChunk wrong(43);
    assert(wrong.next(reader,bytes.size(),out)==Result::WrongChunk);
    assert(wrong.validBytes()==0);
    std::string overflow=bytes+frame;
    Reader extra{overflow};
    RecordChunk full(42);
    for (size_t i=0;i<50;++i) assert(full.next(extra,overflow.size(),out)==Result::Record);
    assert(full.next(extra,overflow.size(),out)==Result::Full);
    assert(!RecordChunk::frame(std::string(RecordChunk::MaxPayload+1,'x'),out));
}

#include <assert.h>
#include <stdio.h>
#include <initializer_list>
#include "RTCMemory.h"
#include "RTCMemoryStream.h"

FakeEsp ESP;
FakeSerial Serial;
FS LittleFS;
using Format = RTCMemoryDetail::Format;

static uint32_t originalCrc(const uint8_t* data, size_t length) {
    uint32_t crc = 0xffffffffu;
    while (length--) {
        uint8_t c = *data++;
        for (uint32_t i = 0x80u; i > 0u; i >>= 1) {
            bool bit = crc & 0x80000000u;
            if (c & i) bit = !bit;
            crc <<= 1;
            if (bit) crc ^= 0x04c11db7u;
        }
    }
    return crc;
}
static void fresh() { memset(ESP.rtc, 0xA5, sizeof(ESP.rtc)); ESP.resetIo(); LittleFS = FS(); }
static void reservedUntouched() { for (size_t i = 0u; i < 128u; ++i) assert(ESP.rtc[i] == 0xA5u); }

template<size_t N> struct Data { uint8_t bytes[N]; };
template<size_t N>
static void crossApi() {
    fresh();
    RTCMemory<Data<N>> buffered;
    assert(buffered.getData() == nullptr && !buffered.save() && !buffered.backup() && !buffered.persist());
    assert(!buffered.begin()); // invalid record initializes a usable zero buffer
    for (size_t i = 0u; i < N; ++i) buffered.getData()->bytes[i] = static_cast<uint8_t>(i * 37u + 9u);
    assert(buffered.save());
    RTCMemoryStream<N> stream;
    uint8_t out[N + 2u]; memset(out, 0xCC, sizeof(out));
    assert(stream.read(0u, out, N) == RtcStatus::NotVerified);
    assert(stream.verify() == RtcStatus::Ok);
    assert(stream.read(0u, out + 1u, N) == RtcStatus::Ok); // unaligned destination
    assert(memcmp(out + 1u, buffered.getData()->bytes, N) == 0);
    assert(out[0] == 0xCCu && out[N + 1u] == 0xCCu);
    for (size_t offset = 0u; offset < N; ++offset) {
        const size_t length = (N - offset > 7u) ? 7u : N - offset;
        assert(stream.read(offset, out + 1u, length) == RtcStatus::Ok);
        assert(memcmp(out + 1u, buffered.getData()->bytes + offset, length) == 0);
    }
    assert(stream.read(N, nullptr, 0u) == RtcStatus::Ok);
    assert(stream.read(N, out, 1u) == RtcStatus::InvalidRange);
    assert(stream.read(size_t(-1), out, 1u) == RtcStatus::InvalidRange);
    assert(stream.read(0u, nullptr, 1u) == RtcStatus::InvalidRange);

    alignas(4) Data<N> source;
    for (size_t i = 0u; i < N; ++i) source.bytes[i] = static_cast<uint8_t>(i * 11u + 2u);
    const RTCMemorySegment segments[] = {RTCMemorySegment::memory(&source, sizeof(source))};
    assert(stream.save(RTCMemorySegments(segments, 1u)) == RtcStatus::Ok);
    assert(stream.read(0u, out, N) == RtcStatus::NotVerified);
    RTCMemory<Data<N>> restored;
    assert(restored.begin());
    assert(memcmp(restored.getData(), &source, N) == 0);
    const size_t aligned = Format::alignedSize(N);
    const uint8_t* record = ESP.rtc + 512u - (12u + aligned);
    assert(Format::get32(record) == 0x4D435452u && Format::get32(record + 4u) == 1u);
    assert(Format::get32(record + 8u) == originalCrc(record + 12u, aligned));
    for (size_t i = N; i < aligned; ++i) assert(record[12u + i] == 0u);
    reservedUntouched();
}

struct GeneratedSource {
    size_t count;
    mutable size_t reads = 0u;
    size_t failAt = 0u;
    explicit GeneratedSource(size_t n) : count(n) {}
    size_t size() const { return count; }
    bool read(size_t offset, void* out, size_t bytes) const {
        if (++reads == failAt) return false;
        uint8_t* p = static_cast<uint8_t*>(out);
        for (size_t i = 0u; i < bytes; ++i) p[i] = static_cast<uint8_t>((offset + i) ^ 0xA5u);
        return true;
    }
};
static void segmentsAndCallbacks() {
    fresh(); RTCMemoryStream<13u> stream;
    const uint8_t a[] = {1u, 2u, 3u};
    alignas(4) const uint8_t b[] = {10u, 11u, 12u, 13u, 14u, 15u, 16u};
    const RTCMemorySegment pieces[] = {RTCMemorySegment::memory(a, 3u), RTCMemorySegment::memory(b, 7u),
        RTCMemorySegment::repeated(0xFFu, 3u)};
    RTCMemorySegments source(pieces, 3u);
    assert(source.size() == 13u && stream.save(source) == RtcStatus::Ok);
    uint8_t expected[13], actual[13];
    assert(source.read(0u, expected, sizeof(expected)));
    assert(!source.read(12u, expected, 2u) && !source.read(0u, nullptr, 1u));
    assert(stream.verify() == RtcStatus::Ok && stream.read(0u, actual, sizeof(actual)) == RtcStatus::Ok);
    assert(memcmp(expected, actual, sizeof(expected)) == 0);
    GeneratedSource generated(13u);
    assert(stream.save(generated) == RtcStatus::Ok && stream.verify() == RtcStatus::Ok);
    assert(stream.read(0u, actual, sizeof(actual)) == RtcStatus::Ok);
    for (size_t i = 0u; i < sizeof(actual); ++i) assert(actual[i] == (i ^ 0xA5u));
    RTCMemorySegments invalid(nullptr, 1u);
    assert(!invalid.read(0u, actual, 1u) && stream.save(invalid) == RtcStatus::InvalidRange);
    const RTCMemorySegment overflow[] = {RTCMemorySegment::repeated(0u, size_t(-1)), RTCMemorySegment::repeated(0u, 1u)};
    RTCMemorySegments overflowSource(overflow, 2u);
    assert(!overflowSource.read(0u, actual, 1u));
    assert(stream.save(overflowSource) == RtcStatus::InvalidRange);
    reservedUntouched();
}
static void corruptionAndFailures() {
    fresh(); RTCMemoryStream<16u> stream; GeneratedSource source(16u);
    assert(stream.save(source) == RtcStatus::Ok);
    uint8_t golden[512]; memcpy(golden, ESP.rtc, sizeof(golden));
    for (size_t i = 512u - 28u; i < 512u; ++i) {
        memcpy(ESP.rtc, golden, sizeof(golden)); ESP.rtc[i] ^= 0x80u;
        assert(stream.verify() == RtcStatus::InvalidRecord);
        uint8_t destination = 0xCCu;
        assert(stream.read(0u, &destination, 1u) == RtcStatus::NotVerified && destination == 0xCCu);
    }
    memcpy(ESP.rtc, golden, sizeof(golden)); ESP.resetIo();
    assert(stream.verify() == RtcStatus::Ok);
    const size_t verifyReads = ESP.reads;
    for (size_t fail = 1u; fail <= verifyReads; ++fail) {
        ESP.resetIo(); ESP.failRead = fail;
        assert(stream.verify() == RtcStatus::IoError);
    }
    ESP.resetIo(); assert(stream.verify() == RtcStatus::Ok);
    alignas(4) uint8_t destination[16];
    ESP.failRead = ESP.reads + 1u;
    assert(stream.read(0u, destination, sizeof(destination)) == RtcStatus::IoError);
    assert(stream.read(0u, destination, sizeof(destination)) == RtcStatus::NotVerified);
    ESP.resetIo(); assert(stream.save(source) == RtcStatus::Ok);
    const size_t writes = ESP.writes;
    for (size_t fail = 1u; fail <= writes; ++fail) {
        memcpy(ESP.rtc, golden, sizeof(golden)); ESP.resetIo(); ESP.failWrite = fail;
        assert(stream.save(source) == RtcStatus::IoError);
        ESP.resetIo(); assert(stream.verify() == RtcStatus::InvalidRecord);
    }
    memcpy(ESP.rtc, golden, sizeof(golden)); ESP.resetIo();
    GeneratedSource failsBeforeWrite(16u); failsBeforeWrite.failAt = 1u;
    assert(stream.save(failsBeforeWrite) == RtcStatus::SourceError && ESP.writes == 0u);
    assert(stream.verify() == RtcStatus::Ok);
    GeneratedSource failsAfterInvalidate(16u); failsAfterInvalidate.failAt = 5u;
    assert(stream.save(failsAfterInvalidate) == RtcStatus::SourceError);
    assert(stream.verify() == RtcStatus::InvalidRecord);
    reservedUntouched();
}
static void bufferedFlashContract() {
    fresh(); RTCMemory<Data<5>> buffered("/data.bin");
    assert(!buffered.begin());
    assert(LittleFS.exists(String("/data.bin")));
    buffered.getData()->bytes[0] = 123u;
    assert(buffered.backup()); // backup refreshes CRC without a save()
    const size_t writes = LittleFS.opensForWrite;
    assert(buffered.backup() && LittleFS.opensForWrite == writes);
    memset(ESP.rtc + 128u, 0, 384u);
    RTCMemory<Data<5>> restored("/data.bin");
    assert(restored.begin() && restored.getData()->bytes[0] == 123u);
    // Both normal replacement and the .tmp recovery remain available.
    LittleFS.rejectOverwrite = true;
    restored.getData()->bytes[0] = 42u; assert(restored.persist());
    assert(LittleFS.rename(String("/data.bin"), String("/data.bin.tmp")));
    memset(ESP.rtc + 128u, 0, 384u);
    RTCMemory<Data<5>> fromTemporary("/data.bin");
    assert(fromTemporary.begin() && fromTemporary.getData()->bytes[0] == 42u);
    assert(LittleFS.exists(String("/data.bin")));
    reservedUntouched();
}
static void transportBounds() {
    RTCMemoryESP8266Io io; alignas(4) uint32_t value = 0u; ESP.resetIo();
    assert(!io.read(1u, &value, 4u) && !io.read(384u, &value, 4u));
    assert(!io.write(size_t(-4), &value, 4u) && !io.write(0u, &value, 0u));
    assert(!io.read(0u, nullptr, 4u));
    assert(ESP.reads == 0u && ESP.writes == 0u);
}
int main() {
    static_assert(sizeof(RTCMemoryStream<372u>) <= 4u, "Stream must not retain a payload mirror");
    const uint8_t known[] = "123456789";
    assert(Format::crc(0xFFFFFFFFu, known, 9u) == 0x0376E6E7u);
    crossApi<1>(); crossApi<2>(); crossApi<3>(); crossApi<4>(); crossApi<5>();
    crossApi<7>(); crossApi<8>(); crossApi<371>(); crossApi<372>();
    segmentsAndCallbacks(); corruptionAndFailures(); bufferedFlashContract(); transportBounds();
    puts("PASS: actual buffered/stream compatibility, CRC oracle, segments/callbacks, alignment/bounds, failures, flash recovery, OTA");
}

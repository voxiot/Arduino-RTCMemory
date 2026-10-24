#pragma once

#include <string.h>
#include "RTCMemoryFormat.h"
#include "RTCMemoryESP8266.h"
#include "RTCMemorySource.h"

enum class RtcStatus : uint8_t {
    Ok, InvalidRecord, InvalidRange, NotVerified, IoError, SourceError
};

// Single-record, synchronous API without a payload mirror, heap, String or FS.
// Source: size() and read(offset, destination, bytes) const. Optional dataAt()
// borrows a contiguous span. Source/RTC must remain stable throughout operations.
// No concurrent writers, yielding transports or ISR calls are supported.
template<size_t PayloadBytes, size_t N = 384u, class Transport = RTCMemoryESP8266Io>
class RTCMemoryStream : private Transport {
    using Format = RTCMemoryDetail::Format;
    static_assert(PayloadBytes > 0u && PayloadBytes <= Format::MaxPayloadBytes, "RTC payload must be 1..372 bytes");
    static constexpr size_t AlignedBytes = Format::alignedSize(PayloadBytes);
    static constexpr size_t RecordBytes = Format::HeaderBytes + AlignedBytes;
    static_assert(RecordBytes <= N && RecordBytes <= Format::AvailableBytes, "RTC record exceeds N or OTA-safe region");
    static constexpr size_t RecordOffset = Format::AvailableBytes - RecordBytes;
    static constexpr size_t PayloadOffset = RecordOffset + Format::HeaderBytes;
  public:
    RTCMemoryStream() : verified_(false) {}
    explicit RTCMemoryStream(const Transport& transport) : Transport(transport), verified_(false) {}

    RtcStatus verify() {
        verified_ = false;
        alignas(4) uint8_t header[12];
        if (!readRaw_(RecordOffset, header, sizeof(header))) return RtcStatus::IoError;
        if (Format::get32(header) != Format::Magic || Format::get32(header + 4u) != Format::Version) return RtcStatus::InvalidRecord;
        uint32_t crc = 0xFFFFFFFFu;
        alignas(4) uint8_t word[4];
        for (size_t offset = 0u; offset < AlignedBytes; offset += 4u) {
            if (!readRaw_(PayloadOffset + offset, word, sizeof(word))) return RtcStatus::IoError;
            crc = Format::crc(crc, word, sizeof(word));
        }
        if (crc != Format::get32(header + 8u)) return RtcStatus::InvalidRecord;
        verified_ = true;
        return RtcStatus::Ok;
    }

    // Bounds are logical payload bounds. Unaligned addresses/lengths are handled
    // internally. An I/O failure may leave destination partially written; status
    // becomes NotVerified until verify() succeeds again. No automatic rollback.
    RtcStatus read(size_t offset, void* destination, size_t bytes) {
        if (!verified_) return RtcStatus::NotVerified;
        if (!Format::range(offset, bytes, PayloadBytes) || (destination == nullptr && bytes != 0u)) return RtcStatus::InvalidRange;
        uint8_t* out = static_cast<uint8_t*>(destination);
        while (bytes != 0u) {
            if ((offset & 3u) == 0u && Format::aligned(out) && bytes >= 4u) {
                const size_t n = bytes & ~size_t{3u};
                if (!readRaw_(PayloadOffset + offset, out, n)) return readFailure_();
                out += n; offset += n; bytes -= n;
            } else {
                alignas(4) uint8_t word[4];
                if (!readRaw_(PayloadOffset + (offset & ~size_t{3u}), word, sizeof(word))) return readFailure_();
                const size_t within = offset & 3u;
                const size_t n = (bytes < 4u - within) ? bytes : 4u - within;
                memcpy(out, word + within, n);
                out += n; offset += n; bytes -= n;
            }
        }
        return RtcStatus::Ok;
    }

    template<class Source>
    RtcStatus save(const Source& source) {
        verified_ = false;
        if (source.size() != PayloadBytes) return RtcStatus::InvalidRange;
        uint32_t crc = 0xFFFFFFFFu;
        alignas(4) uint8_t word[4];
        // Logical payload may not fill its last word: deterministic zero padding.
        for (size_t offset = 0u; offset < AlignedBytes; offset += 4u) {
            if (!sourceWord_(source, offset, word)) return RtcStatus::SourceError;
            crc = Format::crc(crc, word, sizeof(word));
        }
        Format::put32(word, 0u);
        if (!writeRaw_(RecordOffset, word, sizeof(word))) return RtcStatus::IoError;
        size_t offset = 0u;
        while (offset < AlignedBytes) {
            size_t available = 0u;
            const uint8_t* span = span_(source, offset, available, 0);
            const size_t remaining = (offset < PayloadBytes) ? PayloadBytes - offset : 0u;
            const size_t n = ((available < remaining) ? available : remaining) & ~size_t{3u};
            if (span != nullptr && Format::aligned(span) && n != 0u) {
                if (!writeRaw_(PayloadOffset + offset, span, n)) return RtcStatus::IoError;
                offset += n;
            } else {
                if (!sourceWord_(source, offset, word)) return RtcStatus::SourceError;
                if (!writeRaw_(PayloadOffset + offset, word, sizeof(word))) return RtcStatus::IoError;
                offset += 4u;
            }
        }
        alignas(4) uint8_t commit[8];
        Format::put32(commit, Format::Version); Format::put32(commit + 4u, crc);
        if (!writeRaw_(RecordOffset + 4u, commit, sizeof(commit))) return RtcStatus::IoError;
        Format::put32(word, Format::Magic);
        return writeRaw_(RecordOffset, word, sizeof(word)) ? RtcStatus::Ok : RtcStatus::IoError;
    }

  private:
    bool verified_;
    bool readRaw_(size_t offset, void* data, size_t bytes) {
        return static_cast<Transport&>(*this).read(offset, static_cast<uint32_t*>(data), bytes);
    }
    bool writeRaw_(size_t offset, const void* data, size_t bytes) {
        return static_cast<Transport&>(*this).write(offset, static_cast<const uint32_t*>(data), bytes);
    }
    RtcStatus readFailure_() { verified_ = false; return RtcStatus::IoError; }
    template<class Source>
    static bool sourceWord_(const Source& source, size_t offset, uint8_t* word) {
        memset(word, 0, 4u);
        const size_t n = (PayloadBytes - offset < 4u) ? PayloadBytes - offset : 4u;
        return source.read(offset, word, n);
    }
    template<class Source>
    static const uint8_t* span_(const Source&, size_t, size_t& available, long) {
        available = 0u; return nullptr;
    }
    template<class Source>
    static auto span_(const Source& source, size_t offset, size_t& available, int)
        -> decltype(source.dataAt(offset, available)) { return source.dataAt(offset, available); }
};

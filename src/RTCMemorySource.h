#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "RTCMemoryFormat.h"

struct RTCMemorySegment {
    const void* data;
    size_t bytes;
    uint8_t fill;
    static RTCMemorySegment memory(const void* p, size_t n) { return {p, n, 0u}; }
    static RTCMemorySegment repeated(uint8_t value, size_t n) { return {nullptr, n, value}; }
};

// Borrows both the table and its buffers for the duration of save(). A null-data
// segment emits its fill byte; this represents padding without allocating it.
class RTCMemorySegments {
  public:
    RTCMemorySegments(const RTCMemorySegment* segments, size_t count) : segments_(segments), count_(count) {}
    size_t size() const {
        if (segments_ == nullptr && count_ != 0u) return size_t(-1);
        size_t total = 0u;
        for (size_t i = 0u; i < count_; ++i) {
            if (segments_[i].bytes > size_t(-1) - total) return size_t(-1);
            total += segments_[i].bytes;
        }
        return total;
    }
    bool read(size_t offset, void* destination, size_t bytes) const {
        const size_t total = size();
        if (total == size_t(-1) || !RTCMemoryDetail::Format::range(offset, bytes, total)
            || (destination == nullptr && bytes != 0u)) return false;
        uint8_t* out = static_cast<uint8_t*>(destination);
        for (size_t i = 0u; i < count_ && bytes != 0u; ++i) {
            const RTCMemorySegment& s = segments_[i];
            if (offset >= s.bytes) { offset -= s.bytes; continue; }
            const size_t n = (bytes < s.bytes - offset) ? bytes : s.bytes - offset;
            if (s.data != nullptr) memcpy(out, static_cast<const uint8_t*>(s.data) + offset, n);
            else memset(out, s.fill, n);
            out += n; bytes -= n; offset = 0u;
        }
        return bytes == 0u;
    }
    // Optional Source capability for aligned transfers without a scratch copy.
    const uint8_t* dataAt(size_t offset, size_t& available) const {
        available = 0u;
        if (segments_ == nullptr) return nullptr;
        for (size_t i = 0u; i < count_; ++i) {
            const RTCMemorySegment& s = segments_[i];
            if (offset >= s.bytes) { offset -= s.bytes; continue; }
            available = s.bytes - offset;
            return s.data != nullptr ? static_cast<const uint8_t*>(s.data) + offset : nullptr;
        }
        return nullptr;
    }
  private:
    const RTCMemorySegment* segments_;
    size_t count_;
};

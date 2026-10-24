#pragma once

#include <Arduino.h>
#include "RTCMemoryFormat.h"

// Relative byte offsets over the 384 OTA-safe bytes of ESP8266 user RTC.
// Transports implement aligned, positive, word-sized read/write operations.
struct RTCMemoryESP8266Io {
    bool read(size_t offset, uint32_t* out, size_t bytes) {
        return valid(offset, out, bytes) && ESP.rtcUserMemoryRead(
            static_cast<uint32_t>((RTCMemoryDetail::Format::ReservedBytes + offset) / 4u), out, bytes);
    }
    bool write(size_t offset, const uint32_t* data, size_t bytes) {
        // Arduino does not declare the source const; its implementation reads it.
        return valid(offset, data, bytes) && ESP.rtcUserMemoryWrite(
            static_cast<uint32_t>((RTCMemoryDetail::Format::ReservedBytes + offset) / 4u),
            const_cast<uint32_t*>(data), bytes);
    }
  private:
    static bool valid(size_t offset, const void* p, size_t bytes) {
        using RTCMemoryDetail::Format;
        return p != nullptr && bytes != 0u && (offset & 3u) == 0u && (bytes & 3u) == 0u
            && Format::aligned(p) && Format::range(offset, bytes, Format::AvailableBytes);
    }
};

// Borrow a stateful transport for a short-lived stream without copying its state.
template<class Transport>
struct RTCMemoryTransportRef {
    explicit RTCMemoryTransportRef(Transport& transport) : transport_(&transport) {}
    bool read(size_t offset, uint32_t* out, size_t bytes) { return transport_->read(offset, out, bytes); }
    bool write(size_t offset, const uint32_t* data, size_t bytes) { return transport_->write(offset, data, bytes); }
  private:
    Transport* transport_;
};

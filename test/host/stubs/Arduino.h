#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include <sstream>

using byte = uint8_t;
class String {
  public:
    std::string value;
    String(const char* p = "") : value(p) {}
    explicit String(const std::string& p) : value(p) {}
    size_t length() const { return value.size(); }
    friend String operator+(const String& a, const String& b) { return String(a.value + b.value); }
    friend String operator+(const String& a, int b) { std::ostringstream s; s << b; return String(a.value + s.str()); }
};
struct FakeSerial {
    void begin(unsigned long) {}
    template<class T> void print(const T&) {}
    template<class T> void println(const T&) {}
};
extern FakeSerial Serial;
struct FakeEsp {
    uint8_t rtc[512];
    size_t reads = 0u, writes = 0u, failRead = 0u, failWrite = 0u;
    void resetIo() { reads = writes = failRead = failWrite = 0u; }
    bool rtcUserMemoryRead(uint32_t offset, uint32_t* out, size_t bytes) {
        ++reads;
        if (offset * 4u > sizeof(rtc) || bytes > sizeof(rtc) - offset * 4u) return false;
        if (reads == failRead) { memcpy(out, rtc + offset * 4u, bytes / 2u); return false; }
        memcpy(out, rtc + offset * 4u, bytes); return true;
    }
    bool rtcUserMemoryWrite(uint32_t offset, uint32_t* data, size_t bytes) {
        ++writes;
        if (offset * 4u > sizeof(rtc) || bytes > sizeof(rtc) - offset * 4u) return false;
        if (writes == failWrite) { memcpy(rtc + offset * 4u, data, bytes / 2u); return false; }
        memcpy(rtc + offset * 4u, data, bytes); return true;
    }
};
extern FakeEsp ESP;

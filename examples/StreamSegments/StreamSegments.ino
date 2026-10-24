#include <RTCMemoryStream.h>

struct Settings {
    uint32_t schema;
    uint32_t samplePeriodMs;
};
struct Statistics {
    uint32_t bootCount;
    uint32_t sampleCount;
};

Settings settings{1u, 1000u};
Statistics statistics{}; // separate application-owned buffers
RTCMemoryStream<sizeof(Settings) + sizeof(Statistics)> rtc;

void setup() {
    Serial.begin(115200);
    // The RTC layer checks record integrity. The consumer validates its schema
    // before loading the rest, and restores defaults on any failed transfer.
    const bool restored = rtc.verify() == RtcStatus::Ok
        && rtc.read(0u, &settings, sizeof(settings)) == RtcStatus::Ok
        && settings.schema == 1u
        && rtc.read(sizeof(settings), &statistics, sizeof(statistics)) == RtcStatus::Ok;
    if (!restored) { settings = Settings{1u, 1000u}; statistics = Statistics{}; }
    ++statistics.bootCount;
    const RTCMemorySegment parts[] = {
        RTCMemorySegment::memory(&settings, sizeof(settings)),
        RTCMemorySegment::memory(&statistics, sizeof(statistics))
    };
    const RtcStatus status = rtc.save(RTCMemorySegments(parts, 2u));
    Serial.println(static_cast<int>(status));
}

void loop() {}

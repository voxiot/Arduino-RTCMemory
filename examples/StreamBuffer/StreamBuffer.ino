#include <RTCMemoryStream.h>

struct Snapshot {
    uint32_t bootCount;
    uint16_t mode;
    uint16_t reserved;
};

Snapshot snapshot{}; // application-owned buffer
RTCMemoryStream<sizeof(Snapshot)> rtc;

void setup() {
    Serial.begin(115200);
    if (rtc.verify() != RtcStatus::Ok || rtc.read(0u, &snapshot, sizeof(snapshot)) != RtcStatus::Ok) {
        snapshot = Snapshot{}; // also handles a partially failed read
    }
    ++snapshot.bootCount;
    const RTCMemorySegment parts[] = {
        RTCMemorySegment::memory(&snapshot, sizeof(snapshot))
    };
    const RtcStatus status = rtc.save(RTCMemorySegments(parts, 1u));
    Serial.println(static_cast<int>(status)); // 0 = Ok; RTC is volatile on power loss
}

void loop() {}

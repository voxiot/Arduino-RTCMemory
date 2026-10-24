# RTCMemory

[![arduino-library-badge](https://www.ardu-badge.com/badge/RTCMemory.svg)](https://www.ardu-badge.com/RTCMemory) ![Compile Library Examples](https://github.com/fabianoriccardi/RTCMemory/actions/workflows/CompileLibraryExamples.yml/badge.svg)

RTCMemory is a library to ease the access to the RTC memory embedded in ESP8266.

If you are wondering why you should consider an RTC memory or, in general, any low-power memory, you should watch this [video](https://www.youtube.com/watch?v=r-hEOL007nw).

## Motivation

[ESP8266 Arduino core](https://github.com/esp8266/Arduino) provides *byte-oriented* functions to read, write, and check the integrity of data on RTC memory. Hence, accessing data types such as string, integer, or float requires dealing with pointers and casting, leading to hard-to-find errors. So, I created RTCMemory to make this process easier, more intuitive, and error-proof.

## Features

- Simplified access to RTC memory of ESP8266
- OTA-safe placement (never overlaps the first 128 bytes of user RTC memory)
- Integrity header: magic + version + CRC32 (12-byte overhead on RTC memory)
- One-line backup on flash memory (atomic write via temp file + rename)
- Compatible with LittleFS and SPIFFS file systems

## Requirements

RTCMemory v2 requires ESP8266 Arduino core >=2.6.0 or, if you are using PlatformIO, platform espressif8266 >=2.3.0 (that includes ESP8266 Arduino core v2.6.1).

## Installation

You can find RTCMemory on Arduino and PlatformIO library registries. You can install it through your favorite IDEs, or you can use the respective command-line tools running:

    arduino-cli lib install RTCMemory

or:

    pio lib install "fabianoriccardi/RTCMemory"

## Usage

You should be aware of very few APIs to effectively use RTCMemory.  
First, define the data structure that will contain your data (max **372 bytes**, OTA-safe):

    typedef struct {
        int counter;
    } MyData;

Create an instance of `RTCMemory`:

    RTCMemory<MyData> rtcMemory("/path/to/file");

the filepath is an optional parameter, if you don't need the backup on flash memory you can omit it. Then, initialize RTC memory through:

    bool result = rtcMemory.begin();

`result` is true if there are valid data in RTC memory or in flash memory, otherwise is false (the buffer is cleared and still usable via `getData()`).

> :memo: **Note:** Remember to initialize the file system before calling begin().

Get the pointer to the buffer containing your data:

    MyData* myData = rtcMemory.getData();

Modify data as you need. Finally, save data on RTC:

    rtcMemory.save();

Remember that RTC memory is a volatile memory, hence data are lost on power failure. To backup your current buffer to flash, call:

    rtcMemory.backup();

`backup()` refreshes the CRC from the current buffer before writing, so it is safe after modifying `getData()` even without a prior `save()`. Prefer `persist()` when you want both RTC and flash updated in one call:

    rtcMemory.persist();

## Memory layout

ESP8266 has a total amount of 768 bytes of RTC memory. However, the underlying SDK reserves the first 256 bytes, leaving the remaining 512 bytes to the user application.
OTA uses the first 128 bytes of that user region. RTCMemory always keeps that region free: a compile-time `static_assert(OFFSET >= 32)` guarantees the blob starts at byte 128 or higher.

Consequently, the total RTCData size is limited to **384 bytes** (12-byte header + up to **372 bytes** of user payload). The second template parameter `N` (default 384) is a soft limiter for `sizeof(RTCData)` and cannot be used to grow past the OTA-safe budget.

The following diagram illustrates the global RTC memory layout.

![RTC memory layout](extras/rtc_memory_layout.drawio.png "RTC memory layout")

Note that the RTCMemory data are always *shifted* toward the highest addresses of the RTC memory, so that the reserved memory expands toward the lower addresses.

## About the file system

ESP8266 Arduino provides 2 file systems: SPIFFS and LittleFS. However, since April 2020, only LittleFS is officially supported while SPIFFS is deprecated. For this reason, LittleFS becomes the default one since RTCMemory v2. However, you can still use SPIFFS (or any other file system) by specifying it in RTCMemory constructor.

Flash backups are written atomically (`.tmp` file, then rename) to reduce the chance of a corrupted file after power loss during the write.

## Streaming API: reuse application-owned buffers

Include `RTCMemoryStream.h` to use the RTC record without a retained payload mirror:

```cpp
struct State { uint32_t counter; };
State state{};
RTCMemoryStream<sizeof(State)> rtc;

if (rtc.verify() != RtcStatus::Ok ||
    rtc.read(0, &state, sizeof(state)) != RtcStatus::Ok) {
    state = State{};
}
++state.counter;
const RTCMemorySegment parts[] = {
    RTCMemorySegment::memory(&state, sizeof(state))
};
RtcStatus result = rtc.save(RTCMemorySegments(parts, 1));
```

`RTCMemory<T>` retains its existing buffered API and optional filesystem backup.
`RTCMemoryStream<PayloadBytes, N, Transport>` uses only verification state and a
small word scratch, without heap allocation, `String`, FS or a full payload mirror.
The streaming API handles RTC only; consumers retain ownership of their durable
backup/recovery policies. The defaults are `N = 384` and `RTCMemoryESP8266Io`.

### Contract

- `PayloadBytes` is a fixed logical size of 1..372 bytes. The record uses a 12-byte
  header plus payload rounded up to four bytes; that total must fit both `N` and
  the 384 OTA-safe bytes. `N` is a soft limiter, not a region allocator.
- `verify()` checks magic, format version and the complete CRC before permitting
  `read(offset, destination, bytes)`. Payload offsets are byte offsets relative to
  the payload, not hardware word offsets. Zero-length reads are allowed at valid
  offsets after verification. Nonzero reads require a destination.
- `read()` accepts unaligned offsets/buffers/lengths. It uses aligned bulk I/O when
  possible and a four-byte scratch for boundaries. Corrupted records are rejected
  before any destination changes. A later I/O failure can partially modify the
  destination; the caller must restore/reset it, and must verify again before reading.
- `save(source)` requires `source.size()` to equal `PayloadBytes` and a const
  `bool read(size_t offset, void* destination, size_t bytes)` method. The source is
  read twice: for CRC and for writing. It must stay stable for the entire operation.
  An optional `const uint8_t* dataAt(size_t offset, size_t& available) const` exposes
  a borrowed contiguous span for aligned bulk writes; no span allocation is needed.
- `RTCMemorySegments` borrows a table of `RTCMemorySegment` values and their buffers.
  `memory(pointer, bytes)` uses a live buffer; `repeated(value, bytes)` supplies a
  fill region without materializing it. A null data pointer represents a fill segment.
  Callbacks can instead serialize any application representation on demand.
- The streamer writes zero padding after logical payload bytes. Verification uses
  every persisted aligned payload byte, including padding from buffered records.
- The wire header and CRC are shared with `RTCMemory<T>`: magic `0x4D435452`, format
  version 1, MSB-first CRC32 polynomial `0x04C11DB7`, seed `0xFFFFFFFF`, no final XOR.
  Both APIs read each other's records when their logical payload layouts match.
- Saving invalidates verification state. It invalidates magic, writes payload and
  version/CRC, then commits magic last. Interrupted saves leave an invalid record;
  there is no second snapshot/journal. A source error before invalidation leaves
  the previous physical record intact but still requires another `verify()`.
- `RtcStatus` distinguishes `Ok`, `InvalidRecord`, `InvalidRange`, `NotVerified`,
  `IoError` and `SourceError`. Range errors do not write outside caller/RTC bounds.
- Calls are synchronous in the main loop, never an ISR. No yielding transport,
  concurrent writer or modification of borrowed buffers is supported. Verification
  state does not track writes through another instance or external RTC users.
- Records are placed at the high end of user RTC, preserving OTA's first 128 bytes.
  Multiple instances are not allocated separate regions: they may overlap. Aggregate
  multiple buffers into one segmented payload with exclusive ownership of the record.

### Transport and examples

A custom transport implements `read(relativeByteOffset, uint32_t*, bytes)` and
`write(relativeByteOffset, const uint32_t*, bytes)` over a 384-byte region. Requests
are positive and aligned in offset, pointer and length to four bytes. The ESP8266
transport maps it to user RTC bytes 128..511 and enforces those bounds. The record
offset is computed from its size, so smaller payloads remain shifted to the high end.
`RTCMemoryTransportRef<T>` borrows a stateful transport for a short-lived stream.

See `examples/StreamBuffer` for one live structure and `examples/StreamSegments`
for separate settings/statistics buffers and validation of an application schema.

Host regression uses actual `RTCMemory<T>` code with mocked RTC/FS transport:

```sh
g++ -std=c++11 -O2 -Wall -Wextra -Werror -Isrc -Itest/host/stubs test/host/stream_test.cpp -o stream_test
./stream_test
```

It covers both API directions, a separate legacy CRC oracle, sizes 1..372 including
odd sizes/padding, arbitrary reads, sources/segments, corruption, partial I/O,
buffered flash replacement/recovery and OTA bounds. Hardware watchdog/reboot/OTA
validation must still run on an ESP8266.

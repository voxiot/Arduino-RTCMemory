/***************************************************************************
 *   Copyright (C) 2020-2022  Fabiano Riccardi                             *
 *                                                                         *
 *   This file is part of RTCMemory.                                       *
 *                                                                         *
 *   RTCMemory is free software; you can redistribute                      *
 *   it and/or modify it under the terms of the GNU Lesser General Public  *
 *   License as published by the Free Software Foundation; either          *
 *   version 2.1 of the License, or (at your option) any later version.    *
 *                                                                         *
 *   RTCMemory is distributed in the hope that it will be useful,          *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with RTCMemory; if not, see <http://www.gnu.org/licenses/>      *
 ***************************************************************************/
#ifndef RTCMEMORY_H
#define RTCMEMORY_H

#include <LittleFS.h>

/**
 * T is the user data structure, N is the max number of bytes used by this library. N acts like a
 * "soft" limiter.
 *
 * OTA always reserves the first 128 bytes of user RTC memory. This library places its data at the
 * high end of the 512-byte user region and enforces OFFSET >= 32 (blocks), so the total RTCData
 * size cannot exceed 384 bytes.
 */
template<typename T, int N = 384> class RTCMemory {
public:
  /**
   * Create RTCMemory. Provide a valid filepath to enable the backup of RTC memory on flash memory.
   * By default the library uses LittleFS, but you can select an alternative filesystem such as
   * SPIFFS.
   */
  RTCMemory(const String &path = "", FS &fs = LittleFS);

  /**
   * Initialize the buffer using data in RTC memory. If data are invalid, load data from
   * flash. If it fails again, the buffer is cleared. After this call you can safely use this
   * instance (unless a hard RTC read/write failure leaves ready == false, which is rare).
   *
   * Return true if at least one data source is valid, otherwise false (buffer was cleared).
   */
  bool begin();

  /**
   * Write data to RTC memory.
   *
   * Return true if the operation is completed successfully, otherwise false.
   */
  bool save();

  /**
   * Write data to flash memory. The CRC (and header) are refreshed from the current buffer before
   * writing, so backup() is safe to call after modifying getData() without a prior save().
   *
   * To spare flash wear, this call is skipped (and returns true) when the buffer is byte-identical
   * to the last payload successfully written to flash.
   *
   * WARNING: this performs BLOCKING filesystem I/O (open/write/close/rename). Do not call it from
   * time-critical paths or ISRs; it can take tens of milliseconds depending on the filesystem.
   *
   * Return true if the operation is completed successfully (or skipped as unchanged), otherwise
   * false.
   */
  bool backup();

  /**
   * Write data to both RTC and flash RTCMemory. It is equivalent to subsequent call to save() and
   * backup().
   *
   * WARNING: like backup(), this performs BLOCKING filesystem I/O. See backup() for details.
   *
   * Return true if both write operations are completed successfully, otherwise false.
   */
  bool persist();

  /**
   * Get a pointer to the user buffer, structured accordingly to the typename T.
   *
   * Return a valid pointer to the data if begin() was called, otherwise nullptr.
   */
  T *getData();

private:
  const static unsigned int BLOCK_SIZE = 4;

  /**
   * Bytes reserved by OTA at the low end of user RTC memory.
   */
  const static unsigned int OTA_RESERVED_SIZE = 128;

  /**
   * Max size available in RTC user memory.
   */
  const static unsigned int MAX_TOTAL_RTC_MEMORY_SIZE = 512;

  /**
   * Max size of RTCData while keeping the OTA reserved region free.
   */
  const static unsigned int MAX_RTCDATA_SIZE = MAX_TOTAL_RTC_MEMORY_SIZE - OTA_RESERVED_SIZE;

  /**
   * Blob format identifiers. Changing VERSION invalidates previous RTC/flash payloads.
   */
  const static uint32_t RTC_MAGIC   = 0x4D435452;  // 'RTCM' little-endian
  const static uint32_t RTC_VERSION = 1;

  /**
   * Header stored before the user payload (magic + version + crc32).
   */
  const static unsigned int HEADER_SIZE = 12;

  /**
   * Max size of user memory (OTA-safe).
   */
  const static unsigned int MAX_USER_RTC_MEMORY_SIZE = MAX_RTCDATA_SIZE - HEADER_SIZE;

  /**
   * Number of bytes to contain the user data structure. This value is ceiled to multiple of 4 to
   * respect the memory alignment of RTC memory.
   */
  const static unsigned int USER_RTC_MEMORY_SIZE = (sizeof(T) - 1) / BLOCK_SIZE * BLOCK_SIZE + BLOCK_SIZE;

  struct RTCData {
    uint32_t magic;
    uint32_t version;
    uint32_t crc32;
    // The user buffer
    byte data[USER_RTC_MEMORY_SIZE];
  };

  /**
   * Offset from the first block in RTC memory. This offset is used to shift data toward the
   * highest RTC memory addresses.
   */
  const static unsigned int OFFSET = (MAX_TOTAL_RTC_MEMORY_SIZE - sizeof(RTCData)) / BLOCK_SIZE;

  // The buffer
  RTCData rtcData;

  /**
   * Tell if this class is ready to be used i.e. if begin() was called.
   */
  bool ready;

  /**
   * Dirty-check state for the flash backup: CRC of the last payload successfully written to flash
   * and whether such a backup is known to exist. Used to skip redundant writeToFlash() calls.
   */
  bool hasFlashBackup;
  uint32_t lastFlashedCrc;

  /**
   * Verbosity levels:
   * 0 - No output (Default)
   * 1 - Only errors
   * 2 - Debug
   */
  const static int verbosity = 0;

  String filePath;

  FS &fileSystem;

  /**
   * Load data from flash. Tries the primary backup first, then the temp file left behind by an
   * interrupted writeToFlash() (crash between remove() and rename()).
   *
   * Return true if it reads valid data, otherwise false (return false also if filepath is not set).
   */
  bool readFromFlash();

  /**
   * Read and validate a backup file into the buffer.
   *
   * Return true if the file exists, has the expected size and a valid header/CRC.
   */
  bool readFileInto(const String &path);

  /**
   * Save data to flash (atomic: write temp file, then rename).
   *
   * Return true on success, otherwise false (return false also if filepath is not set).
   */
  bool writeToFlash();

  /**
   * Refresh magic/version/CRC and write the buffer to RTC memory.
   */
  bool writeToRtc();

  /**
   * Set magic, version and CRC to match the current user buffer.
   */
  void syncHeaderAndCrc();

  /**
   * Return true if magic, version and CRC match the current buffer.
   */
  bool isValidRtcData() const;

  /**
   * Calculate the CRC (32bit) of the given buffer.
   *
   * Return the CRC code.
   */
  uint32_t calculateCRC32(const uint8_t *data, size_t length) const;

  /**
   * Clear the buffer.
   */
  void clearBuffer();
};

template<typename T, int N>
RTCMemory<T, N>::RTCMemory(const String &path, FS &fs)
  : ready(false), hasFlashBackup(false), lastFlashedCrc(0), filePath(path), fileSystem(fs) {
  static_assert(sizeof(T) <= MAX_USER_RTC_MEMORY_SIZE,
                "The max size of user data is 372 bytes (OTA-safe). "
                "You must reduce the size of your data structure.");
  static_assert(sizeof(RTCData) <= N,
                "You reach the maximum amount of memory reserved to "
                "RTCMemory lib. Reduce the size of your data structure "
                "(OTA requires RTCData <= 384 bytes).");
  static_assert(OFFSET >= OTA_RESERVED_SIZE / BLOCK_SIZE,
                "RTCMemory data overlap the OTA reserved RTC region "
                "(first 128 bytes). Reduce the size of your data structure.");
  static_assert(sizeof(RTCData) <= MAX_RTCDATA_SIZE,
                "RTCData exceeds the OTA-safe RTC memory budget.");
  static_assert(alignof(T) <= BLOCK_SIZE,
                "T requires an alignment greater than 4 bytes (e.g. it contains "
                "uint64_t/double). The RTC buffer is only 4-byte aligned, so such "
                "a type would cause unaligned access faults on the ESP8266. Split "
                "64-bit members into 32-bit halves.");
}

template<typename T, int N> bool RTCMemory<T, N>::begin() {
  if (ready) {
    if (verbosity > 1) Serial.println("Instance already initialized");
    return true;
  }

  if (verbosity > 1) Serial.print("Loading RTC memory... ");

  bool loaded = false;

  if (ESP.rtcUserMemoryRead(OFFSET, (uint32_t *)&rtcData, sizeof(RTCData))) {
    if (isValidRtcData()) {
      loaded = true;
      if (verbosity > 1) Serial.println("CRC is correct");
    } else if (verbosity > 0) {
      Serial.print("RTC memory content is invalid! Trying to restore backup from flash memory..");
    }
  } else if (verbosity > 0) {
    Serial.println("Read RTC memory failure");
  }

  if (!loaded) {
    if (readFromFlash()) {
      loaded = true;
      // The flash backup matches the buffer we just loaded.
      hasFlashBackup = true;
      lastFlashedCrc = rtcData.crc32;
      if (verbosity > 1) Serial.println("Loading backup from flash ok");
      // Keep RTC in sync with the restored backup
      if (!writeToRtc() && verbosity > 0) {
        Serial.println("Restored flash backup but RTC write failed");
      }
    } else {
      if (verbosity > 0) Serial.println("Loading backup from flash FAILED, data are resetted");
      clearBuffer();
      writeToRtc();
      writeToFlash();
      ready = true;
      return false;
    }
  }

  ready = true;

  if (verbosity > 1) Serial.println("Done!");
  return true;
}

template<typename T, int N> bool RTCMemory<T, N>::save() {
  if (ready) {
    return writeToRtc();
  } else {
    if (verbosity > 0) Serial.println("Call begin() before using this method");
    return false;
  }
}

template<typename T, int N> bool RTCMemory<T, N>::backup() {
  if (ready) {
    syncHeaderAndCrc();
    // Skip the write (and the flash wear) when nothing changed since the last backup.
    if (hasFlashBackup && rtcData.crc32 == lastFlashedCrc) { return true; }
    return writeToFlash();
  } else {
    if (verbosity > 0) Serial.println("Call begin() before using this method");
  }
  return false;
}

template<typename T, int N> bool RTCMemory<T, N>::persist() {
  if (ready) {
    if (save()) { return backup(); }
  } else {
    if (verbosity > 0) Serial.println("Call begin() before using this method");
  }
  return false;
}

template<typename T, int N> T *RTCMemory<T, N>::getData() {
  if (ready) { return reinterpret_cast<T *>(rtcData.data); }

  if (verbosity > 0) Serial.println("Call begin() before other calls!");
  return nullptr;
};

template<typename T, int N> bool RTCMemory<T, N>::readFileInto(const String &path) {
  if (!fileSystem.exists(path)) {
    if (verbosity > 0) Serial.println(String("File not existing: ") + path);
    return false;
  }

  File f = fileSystem.open(path, "r");
  if (!f) {
    if (verbosity > 0) Serial.println(String("Error opening file: ") + path);
    return false;
  }

  int byteRead = f.read((uint8_t *)&rtcData, sizeof(RTCData));
  if (verbosity > 1) Serial.println(String("Bytes read:") + byteRead);
  f.close();

  if (byteRead != (int)sizeof(RTCData)) { return false; }
  return isValidRtcData();
}

template<typename T, int N> bool RTCMemory<T, N>::readFromFlash() {
  if (verbosity > 1) Serial.print(String("Reading from ") + filePath + "... ");

  if (filePath.length() == 0) {
    if (verbosity > 1) Serial.println("No filepath was set");
    return false;
  }

  // Primary backup.
  if (readFileInto(filePath)) { return true; }

  // Recovery: on filesystems that cannot rename onto an existing path, writeToFlash() removes the
  // destination before renaming the temp file over it, so a crash in that window leaves only the
  // temp file. Promote it to the final path to restore the backup and close the hole.
  const String tempPath = filePath + ".tmp";
  if (readFileInto(tempPath)) {
    if (verbosity > 0) Serial.println("Recovered backup from temp file");
    if (fileSystem.exists(filePath)) { fileSystem.remove(filePath); }
    if (!fileSystem.rename(tempPath, filePath) && verbosity > 0) {
      Serial.println("Cannot promote temp backup file");
    }
    return true;
  }

  return false;
}

template<typename T, int N> bool RTCMemory<T, N>::writeToFlash() {
  if (verbosity > 1) Serial.print(String("Writing to ") + filePath + "... ");

  if (filePath.length() == 0) {
    if (verbosity > 1) Serial.print("No filepath was set");
    return false;
  }

  const String tempPath = filePath + ".tmp";

  File f = fileSystem.open(tempPath, "w");
  if (!f) {
    if (verbosity > 0) Serial.println("Error writing file. Is the filesystem initialized?");
    return false;
  }

  int n = f.write((uint8_t *)&rtcData, sizeof(RTCData));
  f.close();

  if (n != (int)sizeof(RTCData)) {
    if (verbosity > 0) Serial.println("Cannot write the requested amount of bytes on flash");
    fileSystem.remove(tempPath);
    return false;
  }

  // Atomic replace on LittleFS: lfs_rename overwrites a same-type destination in a single
  // power-loss-safe commit, so no pre-remove is needed (and a pre-remove would open a crash window
  // with no final file). Fall back to remove+rename for filesystems (e.g. SPIFFS) that reject
  // renaming onto an existing path; the .tmp recovery in readFromFlash() backstops that window.
  if (!fileSystem.rename(tempPath, filePath)) {
    if (fileSystem.exists(filePath)) { fileSystem.remove(filePath); }
    if (!fileSystem.rename(tempPath, filePath)) {
      if (verbosity > 0) Serial.println("Cannot rename temp backup file");
      fileSystem.remove(tempPath);
      return false;
    }
  }

  // Remember what is now on flash to short-circuit redundant writes.
  hasFlashBackup = true;
  lastFlashedCrc = rtcData.crc32;

  if (verbosity > 1) Serial.print("Done!");
  return true;
}

template<typename T, int N> bool RTCMemory<T, N>::writeToRtc() {
  syncHeaderAndCrc();

  if (ESP.rtcUserMemoryWrite(OFFSET, (uint32_t *)&rtcData, sizeof(RTCData))) {
    if (verbosity > 1) Serial.println("Write to RTC memory done");
    return true;
  }

  if (verbosity > 0) Serial.println("Write to RTC memory failed");
  return false;
}

template<typename T, int N> void RTCMemory<T, N>::syncHeaderAndCrc() {
  rtcData.magic   = RTC_MAGIC;
  rtcData.version = RTC_VERSION;
  rtcData.crc32   = calculateCRC32((uint8_t *)rtcData.data, USER_RTC_MEMORY_SIZE);
}

template<typename T, int N> bool RTCMemory<T, N>::isValidRtcData() const {
  if (rtcData.magic != RTC_MAGIC || rtcData.version != RTC_VERSION) { return false; }
  return calculateCRC32((uint8_t *)rtcData.data, USER_RTC_MEMORY_SIZE) == rtcData.crc32;
}

template<typename T, int N> uint32_t RTCMemory<T, N>::calculateCRC32(const uint8_t *data, size_t length) const {
  uint32_t crc = 0xffffffff;
  while (length--) {
    uint8_t c = *data++;
    for (uint32_t i = 0x80; i > 0; i >>= 1) {
      bool bit = crc & 0x80000000;
      if (c & i) { bit = !bit; }
      crc <<= 1;
      if (bit) { crc ^= 0x04c11db7; }
    }
  }
  return crc;
}

template<typename T, int N> void RTCMemory<T, N>::clearBuffer() {
  memset((void *)&rtcData.data, 0, USER_RTC_MEMORY_SIZE);
  syncHeaderAndCrc();
}

#endif  // END RTCMEMORY_H

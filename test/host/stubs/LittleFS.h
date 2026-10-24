#pragma once
#include "Arduino.h"
#include <map>
#include <vector>
#include <memory>

class File {
    std::shared_ptr<std::vector<uint8_t>> data_;
    size_t offset_ = 0u;
  public:
    File() {}
    explicit File(std::shared_ptr<std::vector<uint8_t>> data) : data_(data) {}
    explicit operator bool() const { return bool(data_); }
    int read(uint8_t* out, size_t n) {
        if (!data_) return 0;
        n = (n < data_->size() - offset_) ? n : data_->size() - offset_;
        memcpy(out, data_->data() + offset_, n); offset_ += n; return static_cast<int>(n);
    }
    int write(const uint8_t* in, size_t n) {
        if (!data_) return 0;
        data_->insert(data_->end(), in, in + n); return static_cast<int>(n);
    }
    void close() {}
};
class FS {
  public:
    std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> files;
    size_t opensForWrite = 0u;
    bool rejectOverwrite = false;
    bool exists(const String& path) { return files.count(path.value) != 0u; }
    bool remove(const String& path) { return files.erase(path.value) != 0u; }
    bool rename(const String& from, const String& to) {
        const auto found = files.find(from.value);
        if (found == files.end() || (rejectOverwrite && exists(to))) return false;
        files[to.value] = found->second; files.erase(found); return true;
    }
    File open(const String& path, const char* mode) {
        if (mode[0] == 'w') {
            ++opensForWrite;
            const auto data = std::make_shared<std::vector<uint8_t>>(); files[path.value] = data; return File(data);
        }
        const auto found = files.find(path.value); return found == files.end() ? File() : File(found->second);
    }
};
extern FS LittleFS;

#pragma once
#include <Arduino.h>
#include <SD.h>

// Owns the board's global SD/SPI bus. Close all File handles before end().
class SdCard {
public:
    SdCard() = default;
    ~SdCard() { end(); }
    SdCard(const SdCard &) = delete;
    SdCard &operator=(const SdCard &) = delete;
    bool begin(uint32_t frequency_hz = 1000000);
    void end();
    bool is_mounted() const { return mounted; }
    const char *status_text() const;
    File open(const char *path, const char *mode = FILE_READ);
    bool exists(const char *path);
    bool remove(const char *path);
    bool mkdir(const char *path);
    bool rmdir(const char *path);
    bool rename(const char *from, const char *to);
    uint64_t total_bytes();
    uint64_t free_bytes();
    uint32_t frequency() const { return frequency_hz; }
private:
    static SdCard *owner;
    bool mounted = false;
    uint32_t frequency_hz = 1000000;
    char status[96] = "SD: unavailable - see serial";
};

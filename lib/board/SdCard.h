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
    bool begin();
    void end();
    bool is_mounted() const { return mounted; }
    const char *status_text() const;
    File open(const char *path, const char *mode = FILE_READ);
    bool exists(const char *path);
    bool remove(const char *path);
private:
    static SdCard *owner;
    bool mounted = false;
    char status[96] = "SD: unavailable - see serial";
};

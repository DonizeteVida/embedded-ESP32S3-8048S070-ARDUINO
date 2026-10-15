#include "SdCard.h"
#include <SPI.h>

constexpr int kCsGpio = 10;
constexpr int kMosiGpio = 11;
constexpr int kSckGpio = 12;
constexpr int kMisoGpio = 13;
constexpr uint32_t kFrequencyHz = 1000000;
constexpr unsigned kMaxRootEntries = 16;

bool SdCard::begin()
{
    if (mounted) {
        return true;
    }
    if (owner && owner != this) return false;
    owner = this;
    pinMode(kCsGpio, OUTPUT);
    digitalWrite(kCsGpio, HIGH);
    SPI.begin(kSckGpio, kMisoGpio, kMosiGpio, kCsGpio);
    // Never format an unrecognized filesystem. No internal flash partition needed.
    if (!SD.begin(kCsGpio, SPI, kFrequencyHz, "/sd", 5, false) ||
        SD.cardType() == CARD_NONE) {
        Serial.println("SD unavailable: no card or mount failed; insert a FAT-formatted card and reboot");
        end();
        return false;
    }
    File root = SD.open("/", FILE_READ);
    if (!root || !root.isDirectory()) {
        Serial.println("SD unavailable: cannot read root directory");
        root.close();
        end();
        return false;
    }
    mounted = true;
    const uint8_t type = SD.cardType();
    const char *name = type == CARD_MMC ? "MMC" : type == CARD_SD ? "SDSC" :
                       type == CARD_SDHC ? "SDHC/SDXC" : "unknown";
    const unsigned long capacity_mib = static_cast<unsigned long>(SD.cardSize() / (1024 * 1024));
    snprintf(status, sizeof(status), "SD: %s | %lu MiB | SPI 1 MHz", name, capacity_mib);
    Serial.printf("%s; CS%d/MOSI%d/SCK%d/MISO%d, mounted at /sd\n", status,
                  kCsGpio, kMosiGpio, kSckGpio, kMisoGpio);
    Serial.println("SD root (up to 16 entries):");
    unsigned count = 0;
    for (; count < kMaxRootEntries; ++count) {
        File entry = root.openNextFile(FILE_READ);
        if (!entry) {
            break;
        }
        Serial.printf("  %s %s (%lu bytes)\n", entry.isDirectory() ? "DIR " : "FILE",
                      entry.name(), static_cast<unsigned long>(entry.size()));
        entry.close();
    }
    if (count == kMaxRootEntries) {
        Serial.println("  Listing limit reached");
    }
    root.close();
    return true;
}

const char *SdCard::status_text() const
{
    return status;
}

SdCard *SdCard::owner = nullptr;

void SdCard::end()
{
    if (owner != this) return;
    SD.end();
    SPI.end();
    digitalWrite(kCsGpio, HIGH);
    mounted = false;
    snprintf(status, sizeof(status), "SD: unavailable - see serial");
    owner = nullptr;
}

File SdCard::open(const char *path, const char *mode)
{
    return mounted && path && mode ? SD.open(path, mode) : File{};
}
bool SdCard::exists(const char *path) { return mounted && path && SD.exists(path); }
bool SdCard::remove(const char *path) { return mounted && path && SD.remove(path); }

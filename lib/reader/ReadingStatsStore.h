#pragma once
#include "ReaderTypes.h"
namespace reader {
struct ReadingStats {
    uint32_t generation = 0, last_page = 0, total_pages = 0, opening_count = 0;
    uint64_t reading_ms = 0, source_bytes = 0;
    bool completed = false;
};
class ReadingStatsStore {
public:
    explicit ReadingStatsStore(SdCard &card) : card(card) {}
    bool load(const std::string &key, ReadingStats &stats);
    bool save(const std::string &path, const std::string &key, ReadingStats &stats);
    bool for_path(const std::string &path, uint64_t bytes, ReadingStats &stats);
    const std::string &error() const { return failure; }
private:
    SdCard &card;
    std::string failure;
    bool read(const std::string &path, ReadingStats &stats);
    bool write(const std::string &path, const ReadingStats &stats);
    static bool valid_key(const std::string &key);
};
}

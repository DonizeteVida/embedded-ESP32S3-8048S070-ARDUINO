#pragma once
#include "ReaderTypes.h"
namespace reader {
struct DirectoryEntry {
    std::string name;
    uint64_t bytes = 0;
    bool directory = false;
};
// One page of metadata, irrespective of the number of files on the card.
class DirectoryBrowser {
public:
    static constexpr unsigned kRows = 10;
    explicit DirectoryBrowser(SdCard &card) : card(card) {}
    DirectoryBrowser(const DirectoryBrowser &) = delete;
    DirectoryBrowser &operator=(const DirectoryBrowser &) = delete;
    bool open(const std::string &path, unsigned offset = 0);
    bool step(); // Scan at most eight entries per loop, populating a ten-row window.
    bool busy() const { return scanning; }
    void close() { directory.close(); scanning = false; }
    const DirectoryEntry &entry(unsigned i) const { return entries[i]; }
    unsigned count() const { return visible; }
    unsigned total() const { return found; }
    unsigned offset() const { return first; }
    const std::string &path() const { return current_path; }
    const std::string &error() const { return failure; }
private:
    SdCard &card;
    File directory;
    DirectoryEntry entries[kRows];
    std::string current_path = "/", failure;
    unsigned first = 0, found = 0, visible = 0;
    bool scanning = false;
};
}

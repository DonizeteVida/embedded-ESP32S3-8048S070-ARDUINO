#include "DirectoryBrowser.h"
#include <cstdio>
namespace reader {
bool DirectoryBrowser::open(const std::string &path, unsigned offset) {
    close(); failure.clear(); current_path = canonical_path(path); first = offset; visible = found = 0;
    directory = card.open(current_path.c_str());
    if (!directory || !directory.isDirectory()) { directory.close(); failure = "Cannot read this folder. Check the SD card."; return false; }
    scanning = true; return true;
}
bool DirectoryBrowser::step() {
    if (!scanning) return true;
    for (unsigned n = 0; n < 8; ++n) {
        File file = directory.openNextFile(FILE_READ);
        if (!file) { close(); return true; }
        std::string name = basename(file.name());
        bool hidden = current_path == "/" && name == ".comic-reader";
        if (!hidden && name != "." && name != "..") {
            if (found >= first && visible < kRows) {
                auto &item = entries[visible++]; item = DirectoryEntry{};
                item.name = name; // Preserve complete UTF-8 FAT long filenames.
                item.bytes = file.size(); item.directory = file.isDirectory();
            }
            ++found;
        }
        file.close();
    }
    return false;
}
}

#include "ReaderTypes.h"
#include <cctype>
#include <cstdio>

namespace reader {
uint64_t hash_bytes(uint64_t h, const void *data, size_t size) {
    const auto *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}
uint64_t hash_text(const std::string &text) { return hash_bytes(14695981039346656037ULL, text.data(), text.size()); }
std::string canonical_path(const std::string &path) {
    std::string result;
    size_t start = 0;
    while (start < path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        std::string part = path.substr(start, end - start);
        if (part == "..") { size_t last = result.find_last_of('/'); if (last != std::string::npos) result.resize(last); }
        else if (!part.empty() && part != ".") result += "/" + part;
        start = end + 1;
    }
    return result.empty() ? "/" : result;
}
std::string basename(const std::string &path) { return path.substr(path.find_last_of('/') + 1); }
std::string extension(const std::string &path) {
    std::string name = basename(path);
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return "";
    std::string value = name.substr(dot + 1);
    for (char &c : value) c = std::tolower(static_cast<unsigned char>(c));
    return value;
}
bool natural_less(const char *a, const char *b) {
    const char *original_a = a, *original_b = b;
    while (*a && *b) {
        if (std::isdigit(static_cast<unsigned char>(*a)) && std::isdigit(static_cast<unsigned char>(*b))) {
            const char *end_a = a, *end_b = b;
            while (std::isdigit(static_cast<unsigned char>(*end_a))) ++end_a;
            while (std::isdigit(static_cast<unsigned char>(*end_b))) ++end_b;
            while (a < end_a && *a == '0') ++a;
            while (b < end_b && *b == '0') ++b;
            if (end_a - a != end_b - b) return end_a - a < end_b - b;
            int cmp = std::memcmp(a, b, end_a - a);
            if (cmp) return cmp < 0;
            a = end_a; b = end_b;
        } else {
            int ca = std::tolower(static_cast<unsigned char>(*a)), cb = std::tolower(static_cast<unsigned char>(*b));
            if (ca != cb) return ca < cb;
            ++a; ++b;
        }
    }
    if (*a || *b) return !*a;
    return std::strcmp(original_a, original_b) < 0;
}
bool safe_archive_path(const char *name) {
    if (!name || !*name || *name == '/' || *name == '\\' || std::strlen(name) > 255) return false;
    std::string path(name);
    if (path.find('\\') != std::string::npos || path.find(':') != std::string::npos) return false;
    size_t start = 0;
    while (start < path.size()) {
        size_t end = path.find('/', start); if (end == std::string::npos) end = path.size();
        auto part = path.substr(start, end-start);
        if (part == ".." || part == ".") return false;
        for (char c : part) if (static_cast<unsigned char>(c) < 32) return false;
        start = end + 1;
    }
    return true;
}
bool image_name(const char *name) {
    std::string path(name);
    if (path.find("__MACOSX/") == 0 || path.find("/__MACOSX/") != std::string::npos || basename(path).find("._") == 0) return false;
    auto ext = extension(path);
    return ext == "jpg" || ext == "jpeg" || ext == "png";
}
bool prepare_private_storage(SdCard &card, std::string &error) {
    static const char marker[] = "ESP32 comic reader v1\n";
    const std::string owner = std::string(kRoot) + "/owner";
    if (!card.exists(kRoot)) {
        if (!card.mkdir(kRoot)) { error = "Cannot create reader folder. Check SD write access."; return false; }
        File file = card.open(owner.c_str(), FILE_WRITE);
        bool ok = file && file.write(reinterpret_cast<const uint8_t *>(marker), sizeof(marker)-1) == sizeof(marker)-1;
        file.close();
        if (!ok) {
            // These paths were created by this call; permit a clean retry.
            card.remove(owner.c_str());card.rmdir(kRoot);
            error = "Cannot initialize reader folder."; return false;
        }
    }
    File file = card.open(owner.c_str());
    char contents[sizeof(marker)] = {};
    bool owned = file && file.size() == sizeof(marker)-1 && file.read(reinterpret_cast<uint8_t *>(contents), sizeof(marker)-1) == sizeof(marker)-1 && !std::memcmp(contents, marker, sizeof(marker)-1);
    file.close();
    if (!owned) { error = "Reserved reader folder has an unknown owner; nothing was deleted."; return false; }
    for (const char *path : {kCache, kStats}) {
        if (!card.exists(path) && !card.mkdir(path)) { error = "Cannot create reader storage folders."; return false; }
        File dir = card.open(path); bool valid = dir && dir.isDirectory(); dir.close();
        if (!valid) { error = "Reader storage path is not a directory."; return false; }
    }
    return true;
}
std::string book_key(const std::string &path, uint64_t fingerprint) {
    char key[33];
    snprintf(key, sizeof(key), "%016llx%016llx", (unsigned long long)hash_text(canonical_path(path)), (unsigned long long)fingerprint);
    return key;
}
std::string Page::cache_path() const {
    char path[80]; snprintf(path, sizeof(path), "%s/%04lu.%s", kCache, (unsigned long)cache_index, suffix); return path;
}
}

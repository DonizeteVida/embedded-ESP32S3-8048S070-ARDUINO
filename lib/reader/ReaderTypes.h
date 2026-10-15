#pragma once
#include <SdCard.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <new>

namespace reader {
constexpr const char *kRoot = "/.comic-reader";
constexpr const char *kCache = "/.comic-reader/cache";
constexpr const char *kStats = "/.comic-reader/stats";
constexpr size_t kMaxPages = 4096;
constexpr uint32_t kMaxPageBytes = 64UL * 1024 * 1024;

uint64_t hash_bytes(uint64_t hash, const void *bytes, size_t size);
uint64_t hash_text(const std::string &text);
std::string canonical_path(const std::string &path);
std::string basename(const std::string &path);
std::string extension(const std::string &path);
bool natural_less(const char *left, const char *right);
bool safe_archive_path(const char *name);
bool image_name(const char *name);
bool prepare_private_storage(SdCard &card, std::string &error);
std::string book_key(const std::string &path, uint64_t fingerprint);
struct Page {
    char name[256] = {};
    uint32_t bytes = 0;
    uint32_t ordinal = 0;
    uint32_t cache_index = 0;
    char suffix[5] = {};
    std::string cache_path() const;
};
// Explicit allocation failure keeps malformed/large books out of the LVGL heap.
class PageList {
public:
    ~PageList() { clear(); }
    PageList() = default;
    PageList(const PageList &) = delete;
    PageList &operator=(const PageList &) = delete;
    bool allocate() { clear(); data = static_cast<Page *>(heap_caps_malloc(kMaxPages * sizeof(Page), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)); return data != nullptr; }
    void clear() { heap_caps_free(data); data = nullptr; used = 0; }
    bool push(const Page &page) { if (!data || used == kMaxPages) return false; new(data + used++) Page(page); return true; }
    size_t size() const { return used; }
    Page &operator[](size_t i) { return data[i]; }
    const Page &operator[](size_t i) const { return data[i]; }
    Page *begin() { return data; }
    Page *end() { return data ? data + used : nullptr; }
private:
    Page *data = nullptr;
    size_t used = 0;
};
}

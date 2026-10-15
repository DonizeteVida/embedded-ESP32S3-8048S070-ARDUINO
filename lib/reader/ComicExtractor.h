#pragma once
#include "ReaderTypes.h"
namespace reader {
class ComicExtractor {
public:
    enum class Phase { Idle, Clearing, Indexing, Extracting, Ready, Cancelling, Cancelled, PageFailed, Failed };
    explicit ComicExtractor(SdCard &card) : card(card) {}
    ~ComicExtractor();
    bool start(const std::string &source);
    void step();
    // start() indexes metadata only. Extract one requested page; retain the
    // displayed page plus this target, never the rest of the book.
    bool request_page(unsigned index, unsigned keep = UINT32_MAX);
    bool stop_page();
    bool cached(unsigned index) const;
    unsigned cached_count() const;
    unsigned requested_page() const { return target; }
    uint32_t extracted_bytes() const { return written; }
    void cancel();
    void close(); // Call after the reader has released any page references.
    Phase phase() const { return state; }
    bool busy() const { return state == Phase::Clearing || state == Phase::Indexing || state == Phase::Extracting || state == Phase::Cancelling; }
    const std::string &error() const { return failure; }
    const PageList &pages() const { return images; }
    const std::string &key() const { return identity; }
    unsigned done() const { return completed; }
    unsigned scanned() const { return ordinal; }
    const std::string &path() const { return source; }
    // Needed by context-free callback signatures in the pinned unzip library.
    static ComicExtractor *active;
    File zip_file;
private:
    SdCard &card;
    void *zip = nullptr;
    File cache_directory, output;
    PageList images;
    Phase state = Phase::Idle;
    std::string source, failure, identity;
    uint64_t fingerprint = 14695981039346656037ULL;
    uint32_t ordinal = 0, zip_ordinal = 0, written = 0;
    unsigned completed = 0, target = UINT32_MAX;
    unsigned cached_indices[2] = {UINT32_MAX, UINT32_MAX};
    bool partial_owned = false;
    bool member_open = false, zip_open = false;
    uint8_t buffer[4096];
    void fail(const char *message);
    void fail_page(const char *message);
    void release_archive();
    bool clear_one();
    bool open_archive();
    bool preflight();
    friend void *zip_open_file(const char *, int32_t *);
};
}

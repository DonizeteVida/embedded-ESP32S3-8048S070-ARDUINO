#include "ComicExtractor.h"
#include "JobPump.h"
#include <unzipLIB.h>
#include <climits>
#include <cstdio>
#include <new>

namespace reader {
ComicExtractor *ComicExtractor::active = nullptr;
void *zip_open_file(const char *path, int32_t *size) {
    if (!ComicExtractor::active) return nullptr;
    auto *self = ComicExtractor::active;
    self->zip_file = self->card.open(path);
    if (!self->zip_file || self->zip_file.size() > INT32_MAX) { self->zip_file.close(); return nullptr; }
    *size = self->zip_file.size(); return &self->zip_file;
}
static File *zip_handle(void *handle) { return handle ? static_cast<File *>(static_cast<ZIPFILE *>(handle)->fHandle) : nullptr; }
static void zip_close_file(void *handle) { if (auto *file=zip_handle(handle)) file->close(); }
static int32_t zip_read_file(void *handle, uint8_t *data, int32_t length) { auto *file=zip_handle(handle);return file && length>=0 ? file->read(data,length) : -1; }
static int32_t zip_seek_file(void *handle, int32_t position, int type) {
    auto *file = zip_handle(handle);
    if (!file) return -1;
    int64_t target = position;
    if (type == SEEK_CUR) target += file->position();
    else if (type == SEEK_END) target += file->size();
    if (target < 0 || target > INT32_MAX || !file->seek(target)) return -1;
    return file->position();
}
ComicExtractor::~ComicExtractor() { close(); }
void ComicExtractor::release_archive() {
    if (zip) {
        auto *unzip = static_cast<UNZIP *>(zip);
        if (member_open) unzip->closeCurrentFile();
        if (zip_open) unzip->closeZIP();
        unzip->~UNZIP(); heap_caps_free(zip); zip = nullptr;
    }
    member_open = zip_open = false;
    zip_file.close(); output.close(); cache_directory.close();
    if (active == this) active = nullptr;
}
void ComicExtractor::close() { release_archive(); images.clear(); state = Phase::Idle; }
void ComicExtractor::fail(const char *message) {
    failure = message; release_archive();
    // Cleanup is cooperative too; only the marked, flat private cache is touched.
    cache_directory = card.open(kCache);
    state = Phase::Cancelling;
}
bool ComicExtractor::start(const std::string &path) {
    close(); failure.clear(); identity.clear(); source = canonical_path(path);
    fingerprint = 14695981039346656037ULL; expanded = 0; ordinal = zip_ordinal = written = completed = 0;
    if (!prepare_private_storage(card, failure)) { state = Phase::Failed; return false; }
    cache_directory = card.open(kCache);
    if (!cache_directory || !cache_directory.isDirectory()) { failure = "Cannot open page cache."; state = Phase::Failed; return false; }
    state = Phase::Clearing; return true;
}
bool ComicExtractor::clear_one() {
    File entry = cache_directory.openNextFile(FILE_READ);
    if (!entry) { cache_directory.close(); return true; }
    std::string name = basename(entry.name()); bool directory = entry.isDirectory(); entry.close();
    if (directory || name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos) {
        failure = "Unexpected directory in private cache; cleanup stopped."; cache_directory.close(); state = Phase::Failed; return false;
    }
    std::string path = std::string(kCache) + "/" + name;
    if (!card.remove(path.c_str())) { failure = "Cannot remove previous page cache."; cache_directory.close(); state = Phase::Failed; }
    return false;
}
static uint16_t le16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
static uint32_t le32(const uint8_t *p) { return uint32_t(le16(p)) | uint32_t(le16(p+2)) << 16; }
bool ComicExtractor::preflight() {
    File file = card.open(source.c_str());
    if (!file || file.size() < 22 || file.size() > INT32_MAX) { file.close(); failure = "Invalid ZIP or archive exceeds 2 GiB limit."; return false; }
    const uint32_t size=file.size(), length=std::min<uint32_t>(size,65557), first=size-length;
    auto *tail=static_cast<uint8_t *>(heap_caps_malloc(length,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if (!tail) { file.close(); failure="Not enough PSRAM to inspect archive."; return false; }
    bool loaded=file.seek(first);
    for (uint32_t offset=0;loaded && offset<length;) {
        size_t chunk=std::min<uint32_t>(4096,length-offset);
        loaded=file.read(tail+offset,chunk)==int(chunk) && pump_job(); offset+=chunk;
    }
    file.close(); bool found=false;
    if (loaded) for (int32_t offset=length-22;offset>=0;--offset) {
        const uint8_t *record=tail+offset;
        if (le32(record)==0x06054b50 && uint64_t(offset)+22+le16(record+20)==length) {
            found=le16(record+4)==0 && le16(record+6)==0 && le16(record+8)==le16(record+10) &&
                le16(record+10)!=65535 && le32(record+12)!=UINT32_MAX && le32(record+16)!=UINT32_MAX &&
                uint64_t(le32(record+12))+le32(record+16)==first+offset;
            break;
        }
    }
    heap_caps_free(tail);
    if (!found) failure = "Unsupported ZIP64/split archive or damaged ZIP directory.";
    return found;
}
bool ComicExtractor::open_archive() {
    if (!preflight()) return false;
    if (!images.allocate()) { failure = "Not enough PSRAM for comic index."; return false; }
    void *memory = heap_caps_malloc(sizeof(UNZIP), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) { failure = "Not enough PSRAM for ZIP decoder."; return false; }
    zip = new(memory) UNZIP(); active = this;
    auto *unzip = static_cast<UNZIP *>(zip);
    if (unzip->openZIP(source.c_str(), zip_open_file, zip_close_file, zip_read_file, zip_seek_file) != UNZ_OK) { failure = "Cannot read this CBZ archive."; return false; }
    zip_open = true;
    if (unzip->gotoFirstFile() != UNZ_OK) { failure = "This archive has no pages."; return false; }
    return true;
}
void ComicExtractor::cancel() {
    if (!busy()) return;
    failure.clear(); release_archive(); cache_directory = card.open(kCache); state = Phase::Cancelling;
}
void ComicExtractor::step() {
    if (state == Phase::Clearing) {
        if (!clear_one()) return;
        if (!open_archive()) { fail(failure.c_str()); return; }
        state = Phase::Indexing; return;
    }
    if (state == Phase::Cancelling) {
        if (!cache_directory || clear_one()) {
            images.clear();
            if (state != Phase::Failed) state = failure.empty() ? Phase::Cancelled : Phase::Failed;
        }
        return;
    }
    if (!zip) return;
    auto *unzip = static_cast<UNZIP *>(zip);
    if (state == Phase::Indexing) {
        unz_file_info info = {}; char name[256] = {};
        if (unzip->getFileInfo(&info, name, sizeof(name), nullptr, 0, nullptr, 0) != UNZ_OK || info.size_filename >= sizeof(name) || std::strlen(name) != info.size_filename || !safe_archive_path(name)) { fail("Unsafe or oversized filename in archive."); return; }
        if ((info.flag & 0x41) || info.disk_num_start || info.version_needed >= 45 || (info.compression_method != 0 && info.compression_method != 8) || ((info.external_fa >> 16) & 0170000) == 0120000) { fail("Encrypted, split, ZIP64, linked or unsupported ZIP entry."); return; }
        fingerprint = hash_bytes(fingerprint, name, info.size_filename);
        const uint32_t metadata[] = {uint32_t(info.crc), uint32_t(info.uncompressed_size)};
        fingerprint = hash_bytes(fingerprint, metadata, sizeof(metadata));
        if (name[std::strlen(name)-1] != '/' && image_name(name)) {
            if (info.uncompressed_size > kMaxPageBytes || expanded + info.uncompressed_size > kMaxExtractedBytes) { fail("Comic exceeds 64 MiB/page or 1 GiB extracted limit."); return; }
            Page page; snprintf(page.name, sizeof(page.name), "%s", name);
            page.ordinal = ordinal; page.cache_index = images.size(); page.bytes = info.uncompressed_size;
            snprintf(page.suffix, sizeof(page.suffix), "%s", extension(name).c_str());
            if (!images.push(page)) { fail("Comic exceeds the 4096-page limit."); return; }
            expanded += info.uncompressed_size;
        }
        ++ordinal;
        int next = unzip->gotoNextFile();
        if (next == UNZ_END_OF_LIST_OF_FILE) {
            if (!images.size()) { fail("No supported JPEG or PNG pages in this comic."); return; }
            if (card.free_bytes() < expanded + 1024 * 1024) { fail("Not enough free space to extract this comic."); return; }
            identity = book_key(source, fingerprint);
            if (unzip->gotoFirstFile() != UNZ_OK) { fail("Cannot rewind archive."); return; }
            state = Phase::Extracting;
        } else if (next != UNZ_OK || ordinal >= 65535) fail("Damaged or oversized ZIP directory.");
        return;
    }
    if (state == Phase::Extracting) {
        if (!member_open) {
            const Page &page = images[completed];
            if (zip_ordinal < page.ordinal) {
                if (unzip->gotoNextFile() != UNZ_OK) { fail("Cannot locate next page."); return; }
                ++zip_ordinal; return;
            }
            output = card.open(page.cache_path().c_str(), FILE_WRITE);
            if (!output || unzip->openCurrentFile() != UNZ_OK) { fail("Cannot create or decompress cached page."); return; }
            member_open = true; written = 0;
        }
        int count = unzip->readCurrentFile(buffer, sizeof(buffer));
        if (count < 0 || (count > 0 && (written + uint64_t(count) > images[completed].bytes || output.write(buffer, count) != size_t(count)))) { fail("Page extraction failed or SD write was short."); return; }
        if (count > 0) { written += count; return; }
        output.close();
        int crc = unzip->closeCurrentFile(); member_open = false;
        if (crc != UNZ_OK || written != images[completed].bytes) { fail("Page checksum or extracted length failed."); return; }
        if (++completed == images.size()) {
            release_archive();
            std::sort(images.begin(), images.end(), [](const Page &a, const Page &b) { return natural_less(a.name, b.name); });
            state = Phase::Ready;
        }
    }
}
}

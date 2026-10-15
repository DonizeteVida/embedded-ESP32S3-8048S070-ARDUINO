#include "ReadingStatsStore.h"
#include <cstdio>
#include <cctype>
namespace reader {
static std::string stats_path(const std::string &key, const char *suffix) { return std::string(kStats)+"/"+key+suffix; }
static std::string alias_path(const std::string &path) {
    char name[40]; snprintf(name,sizeof(name),"/path-%016llx.link",(unsigned long long)hash_text(canonical_path(path)));
    return std::string(kStats)+name;
}
bool ReadingStatsStore::valid_key(const std::string &key) {
    if (key.size()!=32) return false;
    for (char c:key) if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return true;
}
static int format_record(char *buffer, size_t size, const ReadingStats &s) {
    return snprintf(buffer,size,"{\"version\":1,\"generation\":%lu,\"last_page\":%lu,\"total_pages\":%lu,\"completed\":%u,\"opening_count\":%lu,\"reading_ms\":%llu,\"source_bytes\":%llu",
        (unsigned long)s.generation,(unsigned long)s.last_page,(unsigned long)s.total_pages,unsigned(s.completed),(unsigned long)s.opening_count,
        (unsigned long long)s.reading_ms,(unsigned long long)s.source_bytes);
}
bool ReadingStatsStore::read(const std::string &path, ReadingStats &s) {
    File file=card.open(path.c_str()); char record[384]={};
    size_t size=file?file.size():0;
    if (!file || !size || size>=sizeof(record) || file.read(reinterpret_cast<uint8_t *>(record),size)!=int(size)) { file.close(); return false; }
    file.close();
    unsigned long gen,page,total,opens; unsigned complete;
    unsigned long long ms,bytes,checksum; int end=0;
    int count=sscanf(record,"{\"version\":1,\"generation\":%lu,\"last_page\":%lu,\"total_pages\":%lu,\"completed\":%u,\"opening_count\":%lu,\"reading_ms\":%llu,\"source_bytes\":%llu,\"checksum\":\"%16llx\"}\n%n",
        &gen,&page,&total,&complete,&opens,&ms,&bytes,&checksum,&end);
    if (count!=8 || end!=int(size) || !gen || gen>UINT32_MAX || total>kMaxPages || !total || page>=total || complete>1 || opens>UINT32_MAX) return false;
    ReadingStats candidate; candidate.generation=gen; candidate.last_page=page; candidate.total_pages=total;
    candidate.completed=complete; candidate.opening_count=opens; candidate.reading_ms=ms; candidate.source_bytes=bytes;
    char prefix[320]; format_record(prefix,sizeof(prefix),candidate);
    if (hash_text(prefix)!=checksum) return false;
    s=candidate; return true;
}
bool ReadingStatsStore::write(const std::string &path, const ReadingStats &s) {
    char record[384]; int prefix=format_record(record,sizeof(record),s);
    uint64_t checksum=hash_bytes(14695981039346656037ULL,record,prefix);
    int tail=snprintf(record+prefix,sizeof(record)-prefix,",\"checksum\":\"%016llx\"}\n",(unsigned long long)checksum);
    File file=card.open(path.c_str(),FILE_WRITE);
    bool ok=file && file.write(reinterpret_cast<uint8_t *>(record),prefix+tail)==size_t(prefix+tail);
    file.close(); ReadingStats check;
    return ok && read(path,check) && check.generation==s.generation;
}
bool ReadingStatsStore::load(const std::string &key, ReadingStats &s) {
    failure.clear(); s=ReadingStats{};
    if (!valid_key(key)) { failure="Invalid book identity."; return false; }
    bool found=false,existed=false;
    for (const char *suffix:{".json",".bak",".tmp"}) {
        auto path=stats_path(key,suffix); existed=existed || card.exists(path.c_str());
        ReadingStats candidate;
        if (read(path,candidate) && (!found || candidate.generation>s.generation)) { s=candidate; found=true; }
    }
    if (!found && existed) failure="Saved progress is damaged; starting at page one.";
    return found;
}
bool ReadingStatsStore::save(const std::string &path,const std::string &key,ReadingStats &s) {
    failure.clear();
    if (!valid_key(key) || !s.total_pages || s.total_pages>kMaxPages || s.last_page>=s.total_pages) { failure="Invalid reading statistics."; return false; }
    const auto temporary=stats_path(key,".tmp");
    // A fully written temporary record can be the newest after interrupted rename.
    // Preserve it in its alternating slot before reusing the temporary pathname.
    ReadingStats pending;
    if (read(temporary,pending)) {
        auto slot=stats_path(key,pending.generation%2?".json":".bak"); ReadingStats existing;
        if (!read(slot,existing) || existing.generation<pending.generation) {
            if ((card.exists(slot.c_str()) && !card.remove(slot.c_str())) || !card.rename(temporary.c_str(),slot.c_str())) { failure="Cannot recover saved progress."; return false; }
        }
    }
    ReadingStats previous; load(key,previous);
    ReadingStats next=s; next.generation=std::max(s.generation,previous.generation)+1;
    if (!next.generation || !write(temporary,next)) { failure="Cannot write reading progress."; return false; }
    auto target=stats_path(key,next.generation%2?".json":".bak");
    if ((card.exists(target.c_str()) && !card.remove(target.c_str())) || !card.rename(temporary.c_str(),target.c_str())) { failure="Cannot finalize progress; recovery copy retained."; return false; }
    s=next;
    auto link=alias_path(path), temp_link=link+".tmp", backup=link+".bak";
    File file=card.open(temp_link.c_str(),FILE_WRITE);
    bool ok=file && file.write(reinterpret_cast<const uint8_t *>(key.data()),key.size())==key.size(); file.close();
    if (!ok) { failure="Cannot update library progress index."; return false; }
    if (card.exists(backup.c_str()) && !card.remove(backup.c_str())) { failure="Cannot update progress index backup."; return false; }
    if (card.exists(link.c_str()) && !card.rename(link.c_str(),backup.c_str())) { failure="Cannot preserve progress index."; return false; }
    if (!card.rename(temp_link.c_str(),link.c_str())) { failure="Cannot finalize progress index."; return false; }
    failure.clear(); return true;
}
bool ReadingStatsStore::for_path(const std::string &path,uint64_t bytes,ReadingStats &s) {
    auto link=alias_path(path);
    for (const auto &candidate:{link+".tmp",link,link+".bak"}) {
        File file=card.open(candidate.c_str()); char key[33]={};
        bool ok=file && file.size()==32 && file.read(reinterpret_cast<uint8_t *>(key),32)==32; file.close();
        if (!ok || !valid_key(key)) continue;
        std::string expected=book_key(path,0).substr(0,16);
        if (std::string(key).substr(0,16)==expected && load(key,s) && s.source_bytes==bytes) return true;
    }
    s=ReadingStats{}; return false;
}
}

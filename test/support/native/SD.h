#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <string>
#define FILE_READ "r"
#define FILE_WRITE "w"
constexpr uint8_t CARD_NONE=0,CARD_MMC=1,CARD_SD=2,CARD_SDHC=3;
namespace fake {
inline bool sd_ok=true,root_ok=true,root_directory=true,open_error=false,write_error=false,read_error=false,remove_error=false,rename_error=false,format_requested=false;
inline uint8_t card_type=CARD_SDHC;
inline unsigned sd_begins=0,sd_ends=0,open_files=0,root_entries=0;
inline uint32_t sd_frequency=0;
inline uint64_t capacity=8ULL*1024*1024*1024;
inline std::map<std::string,std::shared_ptr<std::vector<uint8_t>>> files;
inline std::set<std::string> directories={"/"};
inline std::string parent(const std::string &path) { auto slash=path.find_last_of('/');return slash?path.substr(0,slash):"/"; }
inline void reset_sd() {
    sd_ok=root_ok=root_directory=true;open_error=write_error=read_error=remove_error=rename_error=format_requested=false;
    card_type=CARD_SDHC;sd_begins=sd_ends=open_files=root_entries=spi_begins=spi_ends=0;sd_frequency=0;
    files.clear();directories={"/"};capacity=8ULL*1024*1024*1024;
}
}
class File {
    struct Handle {
        std::string name;bool directory=false;size_t pos=0,entry=0;
        std::shared_ptr<std::vector<uint8_t>> data;
        std::vector<std::pair<std::string,bool>> children;
        Handle() { ++fake::open_files; }
        ~Handle() { --fake::open_files; }
    };
    std::shared_ptr<Handle> h;
public:
    File()=default;
    File(const std::string &name,bool dir,std::shared_ptr<std::vector<uint8_t>> data=nullptr) : h(std::make_shared<Handle>()) {
        h->name=name;h->directory=dir;h->data=data;
        if (dir) {
            for (auto &d:fake::directories) if (d!="/" && fake::parent(d)==name) h->children.push_back({d,true});
            for (auto &f:fake::files) if (fake::parent(f.first)==name) h->children.push_back({f.first,false});
        }
    }
    explicit operator bool() const { return !!h; }
    bool isDirectory() const { return h && h->directory; }
    const char *name() const { return h?h->name.c_str():""; }
    size_t size() const { return h && h->data?h->data->size():0; }
    size_t position() const { return h?h->pos:0; }
    bool seek(uint32_t pos) { if (!h || !h->data || pos>h->data->size()) return false;h->pos=pos;return true; }
    void close() { h.reset(); }
    File openNextFile(const char *) {
        if (!h || !h->directory) return {};
        if (fake::root_entries && h->name=="/") { if (h->entry++>=fake::root_entries) return {};return File("entry",false); }
        if (h->entry>=h->children.size()) return {};
        auto child=h->children[h->entry++];
        return File(child.first,child.second,child.second?nullptr:fake::files[child.first]);
    }
    size_t write(const uint8_t *b,size_t n) {
        if (!h || !h->data || fake::write_error) return 0;
        if (h->pos+n>h->data->size()) h->data->resize(h->pos+n);
        memcpy(h->data->data()+h->pos,b,n);h->pos+=n;return n;
    }
    int read(uint8_t *b,size_t n) {
        if (!h || !h->data || fake::read_error) return -1;
        n=std::min(n,h->data->size()-h->pos);memcpy(b,h->data->data()+h->pos,n);h->pos+=n;return n;
    }
};
struct SDClass {
    bool begin(int cs,SPIClass &,uint32_t hz,const char *mount,unsigned max,bool format) {
        ++fake::sd_begins;fake::format_requested=format;fake::sd_frequency=hz;
        return fake::sd_ok && cs==10 && hz>=1000000 && hz<=16000000 && std::string(mount)=="/sd" && max==5;
    }
    void end() { ++fake::sd_ends; }
    uint8_t cardType() { return fake::card_type; }
    uint64_t cardSize() { return fake::capacity; }
    uint64_t totalBytes() { return fake::capacity; }
    uint64_t usedBytes() { uint64_t sum=0;for (auto &f:fake::files) sum+=f.second->size();return sum; }
    File open(const char *path,const char *mode) {
        if (fake::open_error) return {};
        if (std::string(path)=="/") return fake::root_ok?File(path,fake::root_directory):File{};
        if (fake::directories.count(path)) return File(path,true);
        if (std::string(mode)==FILE_WRITE) {
            if (!fake::directories.count(fake::parent(path))) return {};
            fake::files[path]=std::make_shared<std::vector<uint8_t>>();
        }
        auto it=fake::files.find(path);return it==fake::files.end()?File{}:File(path,false,it->second);
    }
    bool exists(const char *path) { return fake::files.count(path) || fake::directories.count(path); }
    bool remove(const char *path) { return !fake::remove_error && fake::files.erase(path); }
    bool mkdir(const char *path) { if (!fake::directories.count(fake::parent(path))) return false;fake::directories.insert(path);return true; }
    bool rmdir(const char *path) {
        for (auto &f:fake::files) if (fake::parent(f.first)==path) return false;
        for (auto &d:fake::directories) if (d!="/" && fake::parent(d)==path) return false;
        return !fake::remove_error && fake::directories.erase(path);
    }
    bool rename(const char *from,const char *to) {
        auto it=fake::files.find(from);
        if (fake::rename_error || it==fake::files.end() || exists(to)) return false;
        fake::files[to]=it->second;fake::files.erase(it);return true;
    }
};
inline SDClass SD;

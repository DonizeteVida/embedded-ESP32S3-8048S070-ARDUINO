#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <map>
#include <vector>
#include <memory>
#include <string>
#define FILE_READ "r"
#define FILE_WRITE "w"
constexpr uint8_t CARD_NONE=0,CARD_MMC=1,CARD_SD=2,CARD_SDHC=3;
namespace fake {
inline bool sd_ok=true,root_ok=true,root_directory=true,open_error=false,write_error=false,read_error=false,remove_error=false,format_requested=false;
inline uint8_t card_type=CARD_SDHC;
inline unsigned sd_begins=0,sd_ends=0,open_files=0,root_entries=0;
inline std::map<std::string,std::shared_ptr<std::vector<uint8_t>>> files;
inline void reset_sd() {
    sd_ok=root_ok=root_directory=true; open_error=write_error=read_error=remove_error=format_requested=false;
    card_type=CARD_SDHC; sd_begins=sd_ends=open_files=root_entries=spi_begins=spi_ends=0; files.clear();
}
}
class File {
    struct Handle {
        std::string name; bool directory=false; size_t pos=0,entry=0;
        std::shared_ptr<std::vector<uint8_t>> data;
        Handle() { ++fake::open_files; }
        ~Handle() { --fake::open_files; }
    };
    std::shared_ptr<Handle> h;
public:
    File()=default;
    File(const std::string &name, bool dir, std::shared_ptr<std::vector<uint8_t>> data=nullptr) : h(std::make_shared<Handle>()) { h->name=name; h->directory=dir; h->data=data; }
    explicit operator bool() const { return !!h; }
    bool isDirectory() const { return h && h->directory; }
    const char *name() const { return h?h->name.c_str():""; }
    size_t size() const { return h && h->data?h->data->size():0; }
    void close() { h.reset(); }
    File openNextFile(const char *) { if (!h || h->entry++>=fake::root_entries) return {}; return File("entry",false); }
    size_t write(const uint8_t *b,size_t n) { if (!h || !h->data || fake::write_error) return 0; h->data->insert(h->data->end(),b,b+n); return n; }
    int read(uint8_t *b,size_t n) { if (!h || !h->data || fake::read_error) return -1; n=std::min(n,h->data->size()-h->pos); memcpy(b,h->data->data()+h->pos,n);h->pos+=n;return n; }
};
struct SDClass {
    bool begin(int cs,SPIClass &,uint32_t hz,const char *mount,unsigned max,bool format) {
        ++fake::sd_begins; fake::format_requested=format;
        return fake::sd_ok && cs==10 && hz==1000000 && std::string(mount)=="/sd" && max==5;
    }
    void end() { ++fake::sd_ends; }
    uint8_t cardType() { return fake::card_type; }
    uint64_t cardSize() { return 8ULL*1024*1024*1024; }
    File open(const char *path,const char *mode) {
        if (fake::open_error) return {};
        if (std::string(path)=="/") return fake::root_ok?File(path,fake::root_directory):File{};
        if (std::string(mode)==FILE_WRITE) fake::files[path]=std::make_shared<std::vector<uint8_t>>();
        auto it=fake::files.find(path); return it==fake::files.end()?File{}:File(path,false,it->second);
    }
    bool exists(const char *path) { return fake::files.count(path); }
    bool remove(const char *path) { return !fake::remove_error && fake::files.erase(path); }
};
inline SDClass SD;

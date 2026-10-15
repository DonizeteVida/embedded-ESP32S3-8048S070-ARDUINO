#pragma once
#include <Arduino.h>
#include <map>
#include <vector>
namespace fake {
inline std::map<uint16_t,std::vector<uint8_t>> registers;
inline bool wire_ok=true, wire_read_error=false, wire_short=false, ack_error=false, reset_detect=false;
inline uint8_t device_address=0x5d;
inline uint16_t short_register=0;
inline unsigned wire_acquisitions=0;
inline unsigned wire_begins=0,wire_ends=0,acks=0;
inline std::vector<std::pair<uint16_t,std::vector<uint8_t>>> writes;
inline void reset_wire() {
    wire_ok=true; wire_read_error=wire_short=ack_error=reset_detect=false;
    device_address=0x5d; short_register=0; wire_acquisitions=0; wire_begins=wire_ends=acks=0; writes.clear();
    registers={{0x8140,{'9','1','1',0}},{0x8048,{0x20,3,0xe0,1}},{0x814e,{0}},{0x814f,{}}};
}
inline void report(std::initializer_list<std::array<uint16_t,3>> points) {
    registers[0x814e]={static_cast<uint8_t>(0x80|points.size())};
    auto &b=registers[0x814f]; b.clear();
    for (auto p:points) b.insert(b.end(),{static_cast<uint8_t>(p[0]),static_cast<uint8_t>(p[1]),static_cast<uint8_t>(p[1]>>8),static_cast<uint8_t>(p[2]),static_cast<uint8_t>(p[2]>>8),0,0,0});
}
}
class TwoWire {
    uint8_t address=0; uint16_t reg=0; size_t cursor=0;
    std::vector<uint8_t> tx,rx;
public:
    bool begin(int sda,int scl,unsigned hz) {
        ++fake::wire_begins;
        bool ok=fake::wire_ok && sda==19 && scl==20 && hz==100000;
        if (ok) ++fake::wire_acquisitions;
        return ok;
    }
    void end() { ++fake::wire_ends; }
    void setTimeOut(unsigned ms) { if (ms!=20) abort(); }
    void beginTransmission(uint8_t a) { address=a; tx.clear(); }
    size_t write(uint8_t b) { tx.push_back(b); return 1; }
    int endTransmission(bool = true) {
        if (address!=fake::device_address || fake::wire_read_error || (fake::reset_detect && fake::now<100)) return 1;
        reg=(tx.at(0)<<8)|tx.at(1);
        if (tx.size()>2) {
            if (fake::ack_error) return 1;
            std::vector<uint8_t> data(tx.begin()+2,tx.end());
            fake::writes.push_back({reg,data}); fake::registers[reg]=data; ++fake::acks;
        }
        return 0;
    }
    size_t requestFrom(uint8_t, size_t length, bool) {
        rx=fake::registers[reg]; if (rx.size()>length) rx.resize(length);
        if ((fake::wire_short || fake::short_register==reg) && !rx.empty()) rx.pop_back(); cursor=0; return rx.size();
    }
    int available() { return rx.size()-cursor; }
    int read() { return cursor<rx.size()?rx[cursor++]:-1; }
};
inline TwoWire Wire;

#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <array>
#define IRAM_ATTR
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define portENTER_CRITICAL_ISR(x) ((void)(x))
#define portEXIT_CRITICAL_ISR(x) ((void)(x))
namespace fake {
inline uint32_t now = 0;
inline bool psram = true, allocation_failure = false;
inline std::array<int, 64> pins{}, modes{};
inline std::function<void(uint32_t)> on_delay;
inline unsigned allocations = 0, frees = 0;
inline void reset_core() {
    now = 0; psram = true; allocation_failure = false; pins.fill(0); modes.fill(0);
    on_delay = nullptr; allocations = frees = 0;
}
}
inline uint32_t millis() { return fake::now; }
inline void delay(uint32_t ms) { fake::now += ms; if (fake::on_delay) fake::on_delay(ms); }
inline void pinMode(int pin, int mode) { fake::modes.at(pin) = mode; }
inline void digitalWrite(int pin, int level) { fake::pins.at(pin) = level; }
inline bool psramFound() { return fake::psram; }
struct FakeEsp {
    size_t getPsramSize() { return 8 * 1024 * 1024; }
    size_t getFreePsram() { return 7 * 1024 * 1024; }
};
inline FakeEsp ESP;
struct FakeSerial {
    void begin(unsigned) {}
    void print(const char *) {}
    void println(const char *) {}
    template<class... Args> void printf(const char *, Args...) {}
};
inline FakeSerial Serial;

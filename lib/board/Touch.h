#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

class Touch {
public:
    static constexpr int kSdaGpio = 19, kSclGpio = 20, kResetGpio = 38, kInterruptGpio = 18;
    static constexpr bool kSwapXY = false, kInvertX = false, kInvertY = false;
    struct State {
        bool available = false, pressed = false;
        uint16_t x = 0, y = 0;
        uint32_t presses = 0, errors = 0;
        uint8_t contacts = 0, contact_id = 0xff;
    };
    Touch() = default;
    ~Touch() { end(); }
    Touch(const Touch &) = delete;
    Touch &operator=(const Touch &) = delete;
    bool begin(lv_display_t *display);
    void end(); // Before Display::end(). Releases this instance's Wire bus.
    State state() const;
    uint8_t device_address() const { return address; }
    uint16_t raw_width_value() const { return raw_width; }
    uint16_t raw_height_value() const { return raw_height; }
private:
    static Touch *owner;
    uint8_t address = 0, active_id = 0xff;
    uint16_t raw_width = 0, raw_height = 0;
    uint32_t last_report_ms = 0, last_log_ms = 0;
    State current;
    lv_indev_t *input = nullptr;
    lv_display_t *attached_display = nullptr;
    bool wire_started = false;
    bool read_register(uint16_t, uint8_t *, size_t);
    bool acknowledge_report();
    bool detect(char *);
    void release(const char *reason);
    void read_error();
    bool map_point(uint16_t, uint16_t, lv_point_t &);
    void poll(lv_indev_data_t *);
    static void read(lv_indev_t *, lv_indev_data_t *);
};

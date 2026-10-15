#pragma once
#include <Arduino.h>
#include <esp_lcd_panel_rgb.h>
#include <lvgl.h>

// One physical RGB panel. All methods except the frame ISR run on the Arduino task.
// Delete attached Touch instances before end(); delete application timers first.
class Display {
public:
    static constexpr int kWidth = 800, kHeight = 480;
    static constexpr uint32_t kPixelClockHz = 16000000;
    static constexpr int kDrawBufferLines = 48;
    static constexpr size_t kFramebufferBytes = kWidth * kHeight * sizeof(uint16_t);
    static constexpr size_t kDrawBufferBytes = kWidth * kDrawBufferLines * sizeof(uint16_t);
    Display() = default;
    ~Display() { end(); }
    Display(const Display &) = delete;
    Display &operator=(const Display &) = delete;
    bool begin(); // Idempotent while healthy; retry after failure releases old resources.
    void end();
    bool show();
    bool healthy();
    uint32_t frame_count();
    lv_display_t *handle() const { return lv_display; }
    static void assert_failed();
private:
    static Display *owner;
    esp_lcd_panel_handle_t panel = nullptr;
    lv_display_t *lv_display = nullptr;
    void *draw_buffer = nullptr;
    bool failed = false, flushed = false;
    volatile uint32_t frames = 0;
    portMUX_TYPE frame_mux = portMUX_INITIALIZER_UNLOCKED;
    void fail(const char *, esp_err_t);
    bool abort_initialization(const char *, esp_err_t);
    void copy(const lv_area_t *, uint8_t *);
    static bool IRAM_ATTR frame_done(esp_lcd_panel_handle_t, esp_lcd_rgb_panel_event_data_t *, void *);
    static void flush(lv_display_t *, const lv_area_t *, uint8_t *);
};

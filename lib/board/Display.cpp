#include "Display.h"
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>

constexpr int kBacklightGpio = 2;
constexpr int kHorizontalPulse = 30;
constexpr int kHorizontalBackPorch = 16;
constexpr int kHorizontalFrontPorch = 210;
constexpr int kVerticalPulse = 13;
constexpr int kVerticalBackPorch = 10;
constexpr int kVerticalFrontPorch = 22;
constexpr uint32_t kFirstFrameTimeoutMs = 250;

static_assert(Display::kDrawBufferBytes == 76800, "RGB565 draw-buffer size");
static_assert(Display::kFramebufferBytes == 768000, "RGB565 framebuffer size");

void Display::fail(const char *stage, esp_err_t error)
{
    failed = true;
    digitalWrite(kBacklightGpio, LOW);
    Serial.printf("Display failure: %s (%s)\n", stage, esp_err_to_name(error));
}

bool Display::abort_initialization(const char *stage, esp_err_t error)
{
    fail(stage, error);
    end();
    return false;
}

bool IRAM_ATTR Display::frame_done(esp_lcd_panel_handle_t, esp_lcd_rgb_panel_event_data_t *, void *context)
{
    auto *self = static_cast<Display *>(context);
    portENTER_CRITICAL_ISR(&self->frame_mux);
    ++self->frames;
    portEXIT_CRITICAL_ISR(&self->frame_mux);
    return false;
}

void Display::flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    auto &self = *static_cast<Display *>(lv_display_get_user_data(display));
    self.copy(area, pixels);
    // Release exactly once, including errors. VSYNC does not own this buffer.
    lv_display_flush_ready(display);
}

void Display::copy(const lv_area_t *area, uint8_t *pixels)
{
    if (!failed) {
        if (area->x1 < 0 || area->y1 < 0 || area->x2 >= Display::kWidth ||
            area->y2 >= Display::kHeight || area->x1 > area->x2 || area->y1 > area->y2) {
            fail("invalid LVGL flush rectangle", ESP_ERR_INVALID_ARG);
        } else {
            // LVGL ends are inclusive; esp_lcd ends are exclusive. This SDK copies
            // into its framebuffer and writes back the PSRAM cache before returning.
            const esp_err_t error = esp_lcd_panel_draw_bitmap(
                panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, pixels);
            if (error == ESP_OK) {
                flushed = true;
            } else {
                fail("RGB framebuffer copy", error);
            }
        }
    }
}

static uint32_t tick_ms()
{
    return millis();
}

static void lvgl_log(lv_log_level_t, const char *message)
{
    Serial.print(message);
}

bool Display::begin()
{
    if (healthy()) return true;
    if (owner != nullptr && owner != this) return false;
    end();
    owner = this;
    failed = false;
    flushed = false;
    frames = 0;
    pinMode(kBacklightGpio, OUTPUT);
    digitalWrite(kBacklightGpio, LOW);
    if (!psramFound() || ESP.getPsramSize() < kFramebufferBytes) {
        return abort_initialization("PSRAM unavailable for RGB framebuffer", ESP_ERR_NO_MEM);
    }

    esp_lcd_rgb_panel_config_t config = {};
    config.clk_src = LCD_CLK_SRC_PLL160M;
    config.timings.pclk_hz = kPixelClockHz;
    config.timings.h_res = kWidth;
    config.timings.v_res = kHeight;
    // JC8048B070N timing table, p. 7: total 1056 clocks x 525 lines.
    config.timings.hsync_pulse_width = kHorizontalPulse;
    config.timings.hsync_back_porch = kHorizontalBackPorch;
    config.timings.hsync_front_porch = kHorizontalFrontPorch;
    config.timings.vsync_pulse_width = kVerticalPulse;
    config.timings.vsync_back_porch = kVerticalBackPorch;
    config.timings.vsync_front_porch = kVerticalFrontPorch;
    config.timings.flags.hsync_idle_low = true;
    config.timings.flags.vsync_idle_low = true;
    config.timings.flags.de_idle_high = false;
    config.timings.flags.pclk_active_neg = true;
    config.timings.flags.pclk_idle_high = false;
    config.data_width = 16;
    config.sram_trans_align = 8;
    config.psram_trans_align = 64;
    config.hsync_gpio_num = 39;
    config.vsync_gpio_num = 40;
    config.de_gpio_num = 41;
    config.pclk_gpio_num = 42;
    // D0..D15: B0..B4, G0..G5, R0..R4. Preserve the board wiring.
    const int data_pins[] = {15, 7, 6, 5, 4, 9, 46, 3, 8, 16, 1, 14, 21, 47, 48, 45};
    for (size_t i = 0; i < sizeof(data_pins) / sizeof(data_pins[0]); ++i) {
        config.data_gpio_nums[i] = data_pins[i];
    }
    config.disp_gpio_num = -1; // GPIO0 belongs to BOOT / revision-dependent audio.
    config.on_frame_trans_done = frame_done;
    config.user_ctx = this;
    config.flags.relax_on_idle = false;
    config.flags.fb_in_psram = true;

    esp_err_t error = esp_lcd_new_rgb_panel(&config, &panel);
    if (error != ESP_OK) {
        return abort_initialization("create RGB panel", error);
    }
    error = esp_lcd_panel_reset(panel);
    if (error != ESP_OK) {
        return abort_initialization("reset RGB panel", error);
    }
    error = esp_lcd_panel_init(panel);
    if (error != ESP_OK) {
        return abort_initialization("initialize RGB panel", error);
    }
    // No esp_lcd_panel_disp_on_off call: unused DISP returns NOT_SUPPORTED.

    draw_buffer = heap_caps_aligned_calloc(
        64, 1, kDrawBufferBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (draw_buffer == nullptr) {
        return abort_initialization("allocate LVGL internal draw buffer", ESP_ERR_NO_MEM);
    }
    if (!lv_is_initialized()) lv_init();
    lv_log_register_print_cb(lvgl_log);
    lv_tick_set_cb(tick_ms);
    lv_display = lv_display_create(kWidth, kHeight);
    if (lv_display == nullptr) {
        return abort_initialization("create LVGL display", ESP_ERR_NO_MEM);
    }
    lv_display_set_color_format(lv_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(lv_display, draw_buffer, nullptr, kDrawBufferBytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(lv_display, this);
    lv_display_set_flush_cb(lv_display, flush);

    constexpr int horizontal_total = kWidth + kHorizontalPulse +
        kHorizontalBackPorch + kHorizontalFrontPorch;
    constexpr int vertical_total = kHeight + kVerticalPulse +
        kVerticalBackPorch + kVerticalFrontPorch;
    Serial.printf("RGB: %dx%d RGB565, requested PCLK %lu Hz, totals %dx%d, nominal %.2f Hz\n",
                  kWidth, kHeight, static_cast<unsigned long>(kPixelClockHz),
                  horizontal_total, vertical_total,
                  static_cast<double>(kPixelClockHz) / horizontal_total / vertical_total);
    Serial.printf("Buffers: RGB PSRAM %u bytes, LVGL internal %u bytes, LVGL heap %u bytes\n",
                  static_cast<unsigned>(kFramebufferBytes),
                  static_cast<unsigned>(kDrawBufferBytes), static_cast<unsigned>(LV_MEM_SIZE));
    return true;
}

bool Display::show()
{
    if (!healthy()) {
        return false;
    }
    lv_refr_now(lv_display);
    if (failed || !flushed) {
        if (!failed) {
            fail("initial LVGL frame not rendered", ESP_FAIL);
        }
        return false;
    }
    // Let the completed image reach a scanout boundary before making it visible.
    const uint32_t first_frame = frame_count();
    const uint32_t started = millis();
    while (frame_count() == first_frame) {
        if (static_cast<uint32_t>(millis() - started) >= kFirstFrameTimeoutMs) {
            fail("no RGB frame-completion interrupt", ESP_ERR_TIMEOUT);
            return false;
        }
        delay(1);
    }
    digitalWrite(kBacklightGpio, HIGH);
    return true;
}

bool Display::healthy()
{
    return !failed && panel != nullptr && lv_display != nullptr;
}

uint32_t Display::frame_count()
{
    portENTER_CRITICAL(&frame_mux);
    const uint32_t snapshot = frames;
    portEXIT_CRITICAL(&frame_mux);
    return snapshot;
}

void Display::assert_failed()
{
    if (owner) owner->fail("LVGL assertion (see preceding LVGL log)", ESP_FAIL);
    abort();
}

Display *Display::owner = nullptr;

void Display::end()
{
    if (owner != this) return;
    digitalWrite(kBacklightGpio, LOW);
    if (lv_display) lv_display_delete(lv_display);
    lv_display = nullptr;
    if (panel) esp_lcd_panel_del(panel); // Deletes the interrupt before instance state is released.
    panel = nullptr;
    heap_caps_free(draw_buffer);
    draw_buffer = nullptr;
    flushed = false;
    owner = nullptr;
}

extern "C" void display_assert_failed(void)
{
    Display::assert_failed();
}

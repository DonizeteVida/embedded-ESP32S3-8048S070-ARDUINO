#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <lvgl.h>

// Hardware configuration, native RGB driver, and LVGL display adapter.
namespace display {

constexpr int kWidth = 800;
constexpr int kHeight = 480;
// Hardware validation: 20/25/33 MHz drifted with this IDF 4.4 PSRAM scanout.
// The vendor's 16 MHz setting is the stable target; see docs/hardware.md.
constexpr uint32_t kPixelClockHz = 16000000;
constexpr int kDrawBufferLines = 48;
constexpr size_t kFramebufferBytes = kWidth * kHeight * sizeof(uint16_t);
constexpr size_t kDrawBufferBytes = kWidth * kDrawBufferLines * sizeof(uint16_t);

// Call once, before creating LVGL objects. Backlight stays off on failure.
bool begin();
// Render the initial UI and wait for a frame boundary before enabling backlight.
bool show();
bool healthy();
uint32_t frame_count();

namespace {

constexpr int kBacklightGpio = 2;
constexpr int kHorizontalPulse = 30;
constexpr int kHorizontalBackPorch = 16;
constexpr int kHorizontalFrontPorch = 210;
constexpr int kVerticalPulse = 13;
constexpr int kVerticalBackPorch = 10;
constexpr int kVerticalFrontPorch = 22;
constexpr uint32_t kFirstFrameTimeoutMs = 250;

static_assert(display::kDrawBufferBytes == 76800, "RGB565 draw-buffer size");
static_assert(display::kFramebufferBytes == 768000, "RGB565 framebuffer size");

esp_lcd_panel_handle_t panel = nullptr;
lv_display_t *lv_display = nullptr;
void *draw_buffer = nullptr;
bool failed = false;
bool flushed = false;
volatile uint32_t frames = 0;
portMUX_TYPE frame_mux = portMUX_INITIALIZER_UNLOCKED;

void fail(const char *stage, esp_err_t error)
{
    failed = true;
    digitalWrite(kBacklightGpio, LOW);
    Serial.printf("Display failure: %s (%s)\n", stage, esp_err_to_name(error));
}

bool abort_initialization(const char *stage, esp_err_t error)
{
    fail(stage, error);
    if (lv_display != nullptr) {
        lv_display_delete(lv_display);
        lv_display = nullptr;
    }
    if (panel != nullptr) {
        esp_lcd_panel_del(panel);
        panel = nullptr;
    }
    heap_caps_free(draw_buffer);
    draw_buffer = nullptr;
    return false;
}

bool IRAM_ATTR frame_done(esp_lcd_panel_handle_t, esp_lcd_rgb_panel_event_data_t *, void *)
{
    portENTER_CRITICAL_ISR(&frame_mux);
    ++frames;
    portEXIT_CRITICAL_ISR(&frame_mux);
    return false;
}

void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    if (!failed) {
        if (area->x1 < 0 || area->y1 < 0 || area->x2 >= display::kWidth ||
            area->y2 >= display::kHeight || area->x1 > area->x2 || area->y1 > area->y2) {
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
    // Release exactly once even on failure, so LVGL cannot deadlock waiting.
    // Scanout frame interrupts do not control LVGL draw-buffer ownership.
    lv_display_flush_ready(display);
}

uint32_t tick_ms()
{
    return millis();
}

void lvgl_log(lv_log_level_t, const char *message)
{
    Serial.print(message);
}

} // namespace

bool begin()
{
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
    lv_init();
    lv_log_register_print_cb(lvgl_log);
    lv_tick_set_cb(tick_ms);
    lv_display = lv_display_create(kWidth, kHeight);
    if (lv_display == nullptr) {
        return abort_initialization("create LVGL display", ESP_ERR_NO_MEM);
    }
    lv_display_set_color_format(lv_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(lv_display, draw_buffer, nullptr, kDrawBufferBytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
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

bool show()
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

bool healthy()
{
    return !failed && panel != nullptr && lv_display != nullptr;
}

uint32_t frame_count()
{
    portENTER_CRITICAL(&frame_mux);
    const uint32_t snapshot = frames;
    portEXIT_CRITICAL(&frame_mux);
    return snapshot;
}

void assert_failed()
{
    fail("LVGL assertion (see preceding LVGL log)", ESP_FAIL);
    abort();
}

} // namespace display

// LVGL's C translation units call this bridge through lv_assert_handler.h.
extern "C" void display_assert_failed(void)
{
    display::assert_failed();
}

// Validation UI, diagnostics, and application lifecycle.
namespace app {
namespace {

bool running = false;

lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y,
                const lv_font_t *font, uint32_t color)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_label_set_text(object, text);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_obj_set_pos(object, x, y);
    return object;
}

lv_obj_t *rectangle(lv_obj_t *parent, int x, int y, int width, int height, uint32_t color)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(object, false);
    lv_obj_set_clickable(object, false);
    return object;
}

void update_uptime(lv_timer_t *timer)
{
    lv_obj_t *uptime = static_cast<lv_obj_t *>(lv_timer_get_user_data(timer));
    lv_label_set_text_fmt(uptime, "Uptime: %lu s", static_cast<unsigned long>(millis() / 1000));
}

void update_progress(void *object, int32_t value)
{
    lv_bar_set_value(static_cast<lv_obj_t *>(object), value, LV_ANIM_OFF);
}

void create_validation_screen()
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0b1220), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(screen, false);

    label(screen, "TL 0,0", 8, 8, &lv_font_montserrat_14, 0x94a3b8);
    lv_obj_t *top_right = label(screen, "TR 799,0", 0, 0, &lv_font_montserrat_14, 0x94a3b8);
    lv_obj_align(top_right, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_t *bottom_left = label(screen, "BL 0,479", 0, 0, &lv_font_montserrat_14, 0x94a3b8);
    lv_obj_align(bottom_left, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lv_obj_t *bottom_right = label(screen, "BR 799,479", 0, 0, &lv_font_montserrat_14, 0x94a3b8);
    lv_obj_align(bottom_right, LV_ALIGN_BOTTOM_RIGHT, -8, -8);

    lv_obj_t *title = label(screen, "", 36, 50, &lv_font_montserrat_28, 0xf8fafc);
    lv_label_set_text_fmt(title, "ESP32-S3 / LVGL %d.%d.%d", lv_version_major(),
                          lv_version_minor(), lv_version_patch());
    char timing[128];
    snprintf(timing, sizeof(timing), "%d x %d  |  RGB565  |  %.2f MHz target / %.2f Hz nominal",
             display::kWidth, display::kHeight,
             static_cast<double>(display::kPixelClockHz) / 1000000.0,
             static_cast<double>(display::kPixelClockHz) / 1056 / 525);
    label(screen, timing, 36, 92, &lv_font_montserrat_14, 0x94a3b8);

    const uint32_t colors[] = {0xff0000, 0x00ff00, 0x0000ff, 0xffffff, 0x000000, 0x808080};
    const char *names[] = {"RED", "GREEN", "BLUE", "WHITE", "BLACK", "GRAY"};
    for (int i = 0; i < 6; ++i) {
        lv_obj_t *swatch = rectangle(screen, 36 + i * 123, 140, 110, 64, colors[i]);
        lv_obj_set_style_border_color(swatch, lv_color_hex(0x64748b), 0);
        lv_obj_set_style_border_width(swatch, 1, 0);
        label(screen, names[i], 36 + i * 123, 210, &lv_font_montserrat_14, 0xcbd5e1);
    }

    for (int i = 0; i < 3; ++i) {
        lv_obj_t *ramp = rectangle(screen, 36, 246 + i * 22, 728, 14, 0x000000);
        lv_obj_set_style_bg_grad_color(ramp, lv_color_hex(colors[i]), 0);
        lv_obj_set_style_bg_grad_dir(ramp, LV_GRAD_DIR_HOR, 0);
    }

    lv_obj_t *uptime = label(screen, "Uptime: 0 s", 36, 332, &lv_font_montserrat_20, 0xf8fafc);
    lv_timer_create(update_uptime, 1000, uptime);
    label(screen, "Continuous scanout / partial LVGL rendering", 36, 362,
          &lv_font_montserrat_14, 0x94a3b8);

    lv_obj_t *progress = lv_bar_create(screen);
    lv_obj_remove_style_all(progress);
    lv_obj_set_pos(progress, 36, 392);
    lv_obj_set_size(progress, 728, 20);
    lv_obj_set_style_bg_color(progress, lv_color_hex(0x1e293b), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(progress, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(progress, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress, lv_color_hex(0x38bdf8), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(progress, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(progress, 10, LV_PART_INDICATOR);

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, progress);
    lv_anim_set_exec_cb(&animation, update_progress);
    lv_anim_set_values(&animation, 0, 100);
    lv_anim_set_duration(&animation, 2000);
    lv_anim_set_reverse_duration(&animation, 2000);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&animation);

    // Draw on the top layer so the exact last row/column remain visible.
    lv_obj_t *perimeter = rectangle(lv_layer_top(), 0, 0, display::kWidth, display::kHeight, 0);
    lv_obj_set_style_bg_opa(perimeter, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(perimeter, lv_color_white(), 0);
    lv_obj_set_style_border_width(perimeter, 1, 0);
}

void log_diagnostics()
{
    static uint32_t last_ms = 0;
    static uint32_t last_frames = 0;
    const uint32_t now = millis();
    const uint32_t elapsed = now - last_ms;
    if (elapsed < 5000) {
        return;
    }
    const uint32_t frames = display::frame_count();
    Serial.printf("RGB frame callbacks: %.2f/s; free internal: %u, largest internal block: %u; "
                  "PSRAM total/free: %u/%u bytes\n",
                  static_cast<double>(frames - last_frames) * 1000.0 / elapsed,
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(ESP.getPsramSize()), static_cast<unsigned>(ESP.getFreePsram()));
    last_ms = now;
    last_frames = frames;
}

} // namespace

void begin()
{
    Serial.begin(115200);
    if (!display::begin()) {
        return;
    }
    create_validation_screen();
    running = display::show();
}

void service()
{
    if (!running || !display::healthy()) {
        delay(1000);
        return;
    }
    lv_timer_handler();
    log_diagnostics();
    delay(5);
}

} // namespace app

void setup()
{
    app::begin();
}

void loop()
{
    app::service();
}

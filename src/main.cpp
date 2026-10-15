#include <Arduino.h>
#include <Wire.h>
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

// GT911 register protocol from the bundled touch drivers; no vendor config writes.
namespace touch {

constexpr int kSdaGpio = 19;
constexpr int kSclGpio = 20;
constexpr int kResetGpio = 38;
constexpr int kInterruptGpio = 18; // Shared with audio LRCLK, including during reset.
constexpr bool kSwapXY = false;
constexpr bool kInvertX = false;
constexpr bool kInvertY = false;

struct State {
    bool available = false;
    bool pressed = false;
    uint16_t x = 0;
    uint16_t y = 0;
    uint32_t presses = 0;
    uint32_t errors = 0;
};

namespace {

constexpr uint16_t kProductIdRegister = 0x8140;
constexpr uint16_t kResolutionRegister = 0x8048;
constexpr uint16_t kStatusRegister = 0x814e;
constexpr uint16_t kPointsRegister = 0x814f;
constexpr uint8_t kMaxContacts = 5;
constexpr uint32_t kStalePressMs = 150;
uint8_t address = 0;
uint8_t active_id = 0xff;
uint16_t raw_width = 0;
uint16_t raw_height = 0;
uint32_t last_report_ms = 0;
State current;
lv_indev_t *input = nullptr;

uint16_t little_endian(const uint8_t *bytes)
{
    return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
}

bool read_register(uint16_t reg, uint8_t *bytes, size_t length)
{
    Wire.beginTransmission(address);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg));
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    const size_t received = Wire.requestFrom(address, length, true);
    if (received != length) {
        while (Wire.available()) {
            Wire.read();
        }
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        bytes[i] = static_cast<uint8_t>(Wire.read());
    }
    return true;
}

bool acknowledge_report()
{
    Wire.beginTransmission(address);
    Wire.write(static_cast<uint8_t>(kStatusRegister >> 8));
    Wire.write(static_cast<uint8_t>(kStatusRegister));
    Wire.write(static_cast<uint8_t>(0));
    return Wire.endTransmission() == 0;
}

bool detect(char *product)
{
    const uint8_t candidates[] = {0x5d, 0x14};
    for (uint8_t candidate : candidates) {
        address = candidate;
        if (read_register(kProductIdRegister, reinterpret_cast<uint8_t *>(product), 4) &&
            product[0] == '9' && product[1] == '1' && product[2] == '1') {
            product[4] = '\0';
            return true;
        }
    }
    address = 0;
    return false;
}

void release()
{
    if (current.pressed) {
        Serial.printf("Touch up: %u,%u\n", current.x, current.y);
    }
    current.pressed = false;
    active_id = 0xff;
}

void read_error()
{
    ++current.errors;
    release();
    static uint32_t last_log_ms = 0;
    if (current.errors == 1 || static_cast<uint32_t>(millis() - last_log_ms) >= 5000) {
        Serial.printf("Touch: invalid report or I2C failure (%lu total)\n",
                      static_cast<unsigned long>(current.errors));
        last_log_ms = millis();
    }
}

bool map_point(uint16_t x, uint16_t y, lv_point_t &point)
{
    // Reject corrupt coordinates rather than turning them into edge presses.
    if (x >= raw_width || y >= raw_height) {
        return false;
    }
    uint16_t width = raw_width;
    uint16_t height = raw_height;
    if (kSwapXY) {
        const uint16_t old_x = x;
        x = y;
        y = old_x;
        width = raw_height;
        height = raw_width;
    }
    point.x = static_cast<uint32_t>(x) * (display::kWidth - 1) / (width - 1);
    point.y = static_cast<uint32_t>(y) * (display::kHeight - 1) / (height - 1);
    if (kInvertX) {
        point.x = display::kWidth - 1 - point.x;
    }
    if (kInvertY) {
        point.y = display::kHeight - 1 - point.y;
    }
    return true;
}

void read(lv_indev_t *, lv_indev_data_t *data)
{
    uint8_t status = 0;
    if (!read_register(kStatusRegister, &status, 1)) {
        read_error();
    } else if ((status & 0x80) != 0) {
        const uint8_t count = status & 0x0f;
        uint8_t points[kMaxContacts * 8] = {};
        bool valid = count <= kMaxContacts;
        if (valid && count != 0) {
            valid = read_register(kPointsRegister, points, count * 8);
        }
        // Only clear a ready report, including empty releases or malformed packets.
        const bool acknowledged = acknowledge_report();
        if (!valid || !acknowledged) {
            read_error();
        } else if (count == 0) {
            release();
            last_report_ms = millis();
        } else {
            // Follow one contact ID so reordered multi-touch packets cannot jump it.
            const uint8_t wanted = current.pressed ? active_id : points[0];
            const uint8_t *selected = nullptr;
            for (uint8_t i = 0; i < count; ++i) {
                if (points[i * 8] == wanted) {
                    selected = &points[i * 8];
                    break;
                }
            }
            lv_point_t point;
            if (selected == nullptr) {
                release();
            } else if (!map_point(little_endian(selected + 1),
                                  little_endian(selected + 3), point)) {
                read_error();
            } else {
                current.x = point.x;
                current.y = point.y;
                if (!current.pressed) {
                    ++current.presses;
                    Serial.printf("Touch down: %u,%u (id %u)\n", current.x, current.y, wanted);
                }
                current.pressed = true;
                active_id = wanted;
            }
            last_report_ms = millis();
        }
    } else if (current.pressed &&
               static_cast<uint32_t>(millis() - last_report_ms) >= kStalePressMs) {
        // A lost release or disconnected controller must not leave LVGL pressed.
        release();
    }
    data->point.x = current.x;
    data->point.y = current.y;
    data->state = current.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = false;
}

} // namespace

bool begin()
{
    if (!Wire.begin(kSdaGpio, kSclGpio, 100000)) {
        Serial.print("Touch unavailable: cannot initialize I2C\n");
        return false;
    }
    Wire.setTimeOut(20);
    char product[5] = {};
    // The music example uses an already-running controller without reset/config writes.
    if (!detect(product)) {
        // Fallback to the TAMC reset sequence with real INT GPIO18, never uint8_t(-1).
        pinMode(kInterruptGpio, OUTPUT);
        pinMode(kResetGpio, OUTPUT);
        digitalWrite(kInterruptGpio, LOW); // Select 7-bit address 0x5d.
        digitalWrite(kResetGpio, LOW);
        delay(10);
        digitalWrite(kResetGpio, HIGH);
        delay(5);
        digitalWrite(kInterruptGpio, LOW);
        delay(50);
        pinMode(kInterruptGpio, INPUT);
        delay(50);
        if (!detect(product)) {
            Serial.print("Touch unavailable: GT911 not found at 0x5d or 0x14; display remains usable\n");
            return false;
        }
    }
    uint8_t resolution[4] = {};
    if (!read_register(kResolutionRegister, resolution, sizeof(resolution))) {
        Serial.print("Touch unavailable: cannot read controller resolution\n");
        return false;
    }
    raw_width = little_endian(resolution);
    raw_height = little_endian(resolution + 2);
    if (raw_width < 2 || raw_height < 2 || raw_width > 4096 || raw_height > 4096 ||
        !acknowledge_report()) {
        Serial.print("Touch unavailable: invalid resolution or status acknowledgement\n");
        return false;
    }
    input = lv_indev_create();
    if (input == nullptr) {
        Serial.print("Touch unavailable: cannot allocate LVGL pointer\n");
        return false;
    }
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(input, lv_display_get_default());
    lv_indev_set_read_cb(input, read);
    lv_timer_set_period(lv_indev_get_read_timer(input), 16);
    current.available = true;
    Serial.printf("Touch: GT911 %.4s at 0x%02x, SDA%d/SCL%d, raw %ux%u, polling 16 ms\n",
                  product, address, kSdaGpio, kSclGpio, raw_width, raw_height);
    return true;
}

State state()
{
    return current;
}

} // namespace touch

// Validation UI, diagnostics, and application lifecycle.
namespace app {
namespace {

bool running = false;
lv_obj_t *touch_status = nullptr;
lv_obj_t *touch_marker = nullptr;
uint32_t tap_count = 0;

void tap_clicked(lv_event_t *event)
{
    lv_obj_t *caption = static_cast<lv_obj_t *>(lv_event_get_user_data(event));
    lv_label_set_text_fmt(caption, "Tap test: %lu", static_cast<unsigned long>(++tap_count));
    Serial.printf("Tap test clicked: %lu\n", static_cast<unsigned long>(tap_count));
}

void update_touch(lv_timer_t *)
{
    const touch::State state = touch::state();
    if (!state.available) {
        lv_label_set_text(touch_status, "Touch unavailable - see serial");
        return;
    }
    lv_label_set_text_fmt(touch_status, "Touch: %s %u,%u | presses %lu | errors %lu",
                          state.pressed ? "DOWN" : "UP", state.x, state.y,
                          static_cast<unsigned long>(state.presses),
                          static_cast<unsigned long>(state.errors));
    lv_obj_set_hidden(touch_marker, !state.pressed);
    if (state.pressed) {
        lv_obj_set_pos(touch_marker, state.x - 5, state.y - 5);
    }
}

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

    lv_obj_t *button = lv_button_create(screen);
    lv_obj_set_pos(button, 542, 326);
    lv_obj_set_size(button, 222, 52);
    lv_obj_t *caption = label(button, "Tap test: 0", 0, 0, &lv_font_montserrat_20, 0xffffff);
    lv_obj_center(caption);
    lv_obj_add_event_cb(button, tap_clicked, LV_EVENT_CLICKED, caption);
    touch_status = label(screen, "", 36, 430, &lv_font_montserrat_14, 0xcbd5e1);
    touch_marker = rectangle(lv_layer_top(), 0, 0, 10, 10, 0xffb020);
    lv_obj_set_style_radius(touch_marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_hidden(touch_marker, true);
    update_touch(nullptr);
    lv_timer_create(update_touch, 50, nullptr);

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
    touch::begin(); // A missing touch controller does not disable the display.
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

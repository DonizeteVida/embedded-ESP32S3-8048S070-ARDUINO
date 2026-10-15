#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <Display.h>
#include <Touch.h>
#include <SdCard.h>

// Validation UI, diagnostics, and application lifecycle.
namespace app {
namespace {

Display display;
Touch touch;
SdCard sdcard;

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
    const Touch::State state = touch.state();
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
             Display::kWidth, Display::kHeight,
             static_cast<double>(Display::kPixelClockHz) / 1000000.0,
             static_cast<double>(Display::kPixelClockHz) / 1056 / 525);
    label(screen, timing, 36, 92, &lv_font_montserrat_14, 0x94a3b8);
    label(screen, sdcard.status_text(), 36, 114, &lv_font_montserrat_14, 0xcbd5e1);

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
    lv_obj_t *perimeter = rectangle(lv_layer_top(), 0, 0, Display::kWidth, Display::kHeight, 0);
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
    const uint32_t frames = display.frame_count();
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
    if (!display.begin()) {
        return;
    }
    touch.begin(display.handle()); // A missing touch controller does not disable the display.
    sdcard.begin(); // Optional storage; mount failure leaves the demo usable.
    create_validation_screen();
    running = display.show();
}

void service()
{
    if (!running || !display.healthy()) {
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

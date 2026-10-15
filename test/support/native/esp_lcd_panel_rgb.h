#pragma once
#include <Arduino.h>
#include <vector>
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NO_MEM = 1, ESP_ERR_INVALID_ARG = 2, ESP_ERR_TIMEOUT = 3;
constexpr int LCD_CLK_SRC_PLL160M = 0;
inline const char *esp_err_to_name(int) { return "fake SDK error"; }
using esp_lcd_panel_handle_t = void *;
struct esp_lcd_rgb_panel_event_data_t {};
struct esp_lcd_rgb_panel_config_t {
    int clk_src = 0;
    struct {
        uint32_t pclk_hz = 0;
        int h_res=0,v_res=0,hsync_pulse_width=0,hsync_back_porch=0,hsync_front_porch=0;
        int vsync_pulse_width=0,vsync_back_porch=0,vsync_front_porch=0;
        struct { bool hsync_idle_low=false,vsync_idle_low=false,de_idle_high=false,pclk_active_neg=false,pclk_idle_high=false; } flags;
    } timings;
    size_t data_width=0,sram_trans_align=0,psram_trans_align=0;
    int hsync_gpio_num=0,vsync_gpio_num=0,de_gpio_num=0,pclk_gpio_num=0,data_gpio_nums[16]{},disp_gpio_num=0;
    bool (*on_frame_trans_done)(esp_lcd_panel_handle_t,esp_lcd_rgb_panel_event_data_t *,void *) = nullptr;
    void *user_ctx = nullptr;
    struct { bool relax_on_idle=false,fb_in_psram=false; } flags;
};
namespace fake {
inline esp_lcd_rgb_panel_config_t rgb;
inline int create_error=0,reset_error=0,init_error=0,copy_error=0;
inline unsigned panels=0,deleted_panels=0,copies=0;
inline int last_x2=0,last_y2=0;
inline std::vector<uint16_t> framebuffer;
inline void frame() { if (panels && rgb.on_frame_trans_done) rgb.on_frame_trans_done((void *)1,nullptr,rgb.user_ctx); }
inline void reset_lcd() {
    rgb = {}; create_error=reset_error=init_error=copy_error=0;
    panels=deleted_panels=copies=0; last_x2=last_y2=0;
    framebuffer.assign(800*480, 0xa55a);
}
}
inline int esp_lcd_new_rgb_panel(const esp_lcd_rgb_panel_config_t *c, void **out) {
    fake::rgb=*c; if (fake::create_error) return fake::create_error;
    *out=(void *)1; ++fake::panels; return ESP_OK;
}

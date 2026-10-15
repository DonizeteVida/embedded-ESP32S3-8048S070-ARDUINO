#pragma once
#include <esp_lcd_panel_rgb.h>
inline int esp_lcd_panel_reset(void *) { return fake::reset_error; }
inline int esp_lcd_panel_init(void *) { return fake::init_error; }
inline int esp_lcd_panel_del(void *) { --fake::panels; ++fake::deleted_panels; return 0; }
inline int esp_lcd_panel_draw_bitmap(void *, int x1, int y1, int x2, int y2, const void *pixels) {
    ++fake::copies; fake::last_x2=x2; fake::last_y2=y2;
    if (fake::copy_error) return fake::copy_error;
    auto p=static_cast<const uint16_t *>(pixels);
    for (int y=y1; y<y2; ++y) for (int x=x1; x<x2; ++x) fake::framebuffer.at(y*800+x)=*p++;
    return 0;
}

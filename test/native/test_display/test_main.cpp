#include <unity.h>
#include <Display.h>
#include <esp_lcd_panel_ops.h>
#include <esp_heap_caps.h>
#include <src/display/lv_display_private.h>

static Display display;
void setUp() {
    display.end(); fake::reset_core(); fake::reset_lcd();
    fake::on_delay=[](uint32_t) { fake::frame(); };
}
void tearDown() { display.end(); TEST_ASSERT_EQUAL(fake::allocations,fake::frees); TEST_ASSERT_EQUAL(0,fake::panels); }
static void config_and_lifecycle() {
    TEST_ASSERT_TRUE(display.begin());
    auto &c=fake::rgb;
    TEST_ASSERT_EQUAL(16000000,c.timings.pclk_hz);
    TEST_ASSERT_EQUAL(800,c.timings.h_res); TEST_ASSERT_EQUAL(480,c.timings.v_res);
    TEST_ASSERT_EQUAL(30,c.timings.hsync_pulse_width); TEST_ASSERT_EQUAL(16,c.timings.hsync_back_porch); TEST_ASSERT_EQUAL(210,c.timings.hsync_front_porch);
    TEST_ASSERT_EQUAL(13,c.timings.vsync_pulse_width); TEST_ASSERT_EQUAL(10,c.timings.vsync_back_porch); TEST_ASSERT_EQUAL(22,c.timings.vsync_front_porch);
    TEST_ASSERT_TRUE(c.timings.flags.pclk_active_neg); TEST_ASSERT_TRUE(c.timings.flags.hsync_idle_low); TEST_ASSERT_TRUE(c.timings.flags.vsync_idle_low);
    TEST_ASSERT_FALSE(c.timings.flags.de_idle_high); TEST_ASSERT_FALSE(c.timings.flags.pclk_idle_high);
    const int pins[]={15,7,6,5,4,9,46,3,8,16,1,14,21,47,48,45};
    TEST_ASSERT_EQUAL_INT_ARRAY(pins,c.data_gpio_nums,16);
    TEST_ASSERT_EQUAL(39,c.hsync_gpio_num); TEST_ASSERT_EQUAL(40,c.vsync_gpio_num);
    TEST_ASSERT_EQUAL(41,c.de_gpio_num); TEST_ASSERT_EQUAL(42,c.pclk_gpio_num); TEST_ASSERT_EQUAL(-1,c.disp_gpio_num);
    TEST_ASSERT_TRUE(c.flags.fb_in_psram); TEST_ASSERT_FALSE(c.flags.relax_on_idle);
    TEST_ASSERT_EQUAL(16,c.data_width); TEST_ASSERT_EQUAL(8,c.sram_trans_align); TEST_ASSERT_EQUAL(64,c.psram_trans_align);
    TEST_ASSERT_EQUAL_PTR(&display,c.user_ctx);
    auto *handle=display.handle(); TEST_ASSERT_TRUE(display.begin()); TEST_ASSERT_EQUAL_PTR(handle,display.handle()); TEST_ASSERT_EQUAL(1,fake::panels);
    Display other; TEST_ASSERT_FALSE(other.begin()); other.end(); TEST_ASSERT_TRUE(display.healthy());
    fake::frame(); TEST_ASSERT_EQUAL(1,display.frame_count());
    display.end(); display.end(); TEST_ASSERT_FALSE(display.healthy()); TEST_ASSERT_NULL(display.handle());
    TEST_ASSERT_TRUE(display.begin()); TEST_ASSERT_EQUAL(0,display.frame_count());
}
static void initialization_failures() {
    fake::psram=false; TEST_ASSERT_FALSE(display.begin()); TEST_ASSERT_EQUAL(LOW,fake::pins[2]); fake::psram=true;
    fake::create_error=ESP_FAIL; TEST_ASSERT_FALSE(display.begin()); fake::create_error=0;
    fake::reset_error=ESP_FAIL; TEST_ASSERT_FALSE(display.begin()); fake::reset_error=0;
    fake::init_error=ESP_FAIL; TEST_ASSERT_FALSE(display.begin()); fake::init_error=0;
    fake::allocation_failure=true; TEST_ASSERT_FALSE(display.begin()); fake::allocation_failure=false;
    TEST_ASSERT_EQUAL(0,fake::panels); TEST_ASSERT_FALSE(display.show());
    TEST_ASSERT_TRUE(display.begin()); TEST_ASSERT_TRUE(display.healthy());
}
static void rendered_rgb565_and_edges() {
    TEST_ASSERT_TRUE(display.begin());
    lv_obj_t *screen=lv_display_get_screen_active(display.handle());
    lv_obj_set_style_bg_color(screen,lv_color_hex(0xff0000),0);
    lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
    lv_obj_set_style_border_color(screen,lv_color_white(),0);
    lv_obj_set_style_border_width(screen,1,0); lv_obj_set_style_radius(screen,0,0);
    TEST_ASSERT_TRUE(display.show()); TEST_ASSERT_EQUAL(HIGH,fake::pins[2]);
    TEST_ASSERT_GREATER_THAN(0,fake::copies);
    TEST_ASSERT_EQUAL_HEX16(0xf800,fake::framebuffer[240*800+400]);
    for (int x=0;x<800;++x) { TEST_ASSERT_EQUAL_HEX16(0xffff,fake::framebuffer[x]); TEST_ASSERT_EQUAL_HEX16(0xffff,fake::framebuffer[479*800+x]); }
    for (int y=0;y<480;++y) { TEST_ASSERT_EQUAL_HEX16(0xffff,fake::framebuffer[y*800]); TEST_ASSERT_EQUAL_HEX16(0xffff,fake::framebuffer[y*800+799]); }
    const uint32_t colors[]={0x00ff00,0x0000ff,0xffffff,0x000000};
    const uint16_t expected[]={0x07e0,0x001f,0xffff,0};
    for (unsigned i=0;i<4;++i) { lv_obj_set_style_bg_color(screen,lv_color_hex(colors[i]),0); TEST_ASSERT_TRUE(display.show()); TEST_ASSERT_EQUAL_HEX16(expected[i],fake::framebuffer[240*800+400]); }
}
static void flush_pixel_bounds_and_errors() {
    TEST_ASSERT_TRUE(display.begin());
    auto *h=display.handle(); uint16_t pixel=0x1234;
    lv_area_t area={799,479,799,479}; h->flushing=1;
    h->flush_cb(h,&area,reinterpret_cast<uint8_t *>(&pixel));
    TEST_ASSERT_EQUAL(0,h->flushing); TEST_ASSERT_EQUAL(800,fake::last_x2); TEST_ASSERT_EQUAL(480,fake::last_y2);
    TEST_ASSERT_EQUAL_HEX16(pixel,fake::framebuffer.back());
    area={0,0,800,0}; unsigned copies=fake::copies; h->flushing=1;
    h->flush_cb(h,&area,reinterpret_cast<uint8_t *>(&pixel));
    TEST_ASSERT_EQUAL(0,h->flushing); TEST_ASSERT_EQUAL(copies,fake::copies);
    TEST_ASSERT_FALSE(display.healthy()); TEST_ASSERT_EQUAL(LOW,fake::pins[2]);
    h->flushing=1; h->flush_cb(h,&area,reinterpret_cast<uint8_t *>(&pixel)); TEST_ASSERT_EQUAL(0,h->flushing);
}
static void copy_failure_and_missing_interrupt() {
    TEST_ASSERT_TRUE(display.begin()); fake::copy_error=ESP_FAIL;
    TEST_ASSERT_FALSE(display.show()); TEST_ASSERT_FALSE(display.healthy()); TEST_ASSERT_EQUAL(LOW,fake::pins[2]);
    fake::copy_error=0; TEST_ASSERT_TRUE(display.begin()); fake::on_delay=nullptr;
    uint32_t start=millis(); TEST_ASSERT_FALSE(display.show()); TEST_ASSERT_GREATER_OR_EQUAL(250,millis()-start);
    TEST_ASSERT_FALSE(display.healthy()); TEST_ASSERT_EQUAL(LOW,fake::pins[2]);
}
static void portrait_rotation_pixels_bounds_and_rendering() {
    TEST_ASSERT_TRUE(display.begin(LV_DISPLAY_ROTATION_90));
    auto *h=display.handle();TEST_ASSERT_EQUAL(480,lv_display_get_horizontal_resolution(h));TEST_ASSERT_EQUAL(800,lv_display_get_vertical_resolution(h));
    lv_area_t area={0,0,1,2};uint16_t pixels[]={1,2,3,4,5,6};h->flushing=1;
    h->flush_cb(h,&area,reinterpret_cast<uint8_t *>(pixels));
    TEST_ASSERT_EQUAL(0,h->flushing);
    const uint16_t top[]={2,4,6},bottom[]={1,3,5};
    TEST_ASSERT_EQUAL_UINT16_ARRAY(top,&fake::framebuffer[478*800],3);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(bottom,&fake::framebuffer[479*800],3);
    auto *screen=lv_display_get_screen_active(h);lv_obj_set_style_bg_color(screen,lv_color_hex(0x00ff00),0);
    TEST_ASSERT_TRUE(display.show());TEST_ASSERT_EQUAL_HEX16(0x07e0,fake::framebuffer[240*800+400]);
    area={0,0,480,0};h->flushing=1;h->flush_cb(h,&area,reinterpret_cast<uint8_t *>(pixels));TEST_ASSERT_EQUAL(0,h->flushing);TEST_ASSERT_FALSE(display.healthy());
}
int main() {
    UNITY_BEGIN(); RUN_TEST(portrait_rotation_pixels_bounds_and_rendering); RUN_TEST(config_and_lifecycle); RUN_TEST(initialization_failures);
    RUN_TEST(rendered_rgb565_and_edges); RUN_TEST(flush_pixel_bounds_and_errors); RUN_TEST(copy_failure_and_missing_interrupt);
    return UNITY_END();
}

#pragma once
#include <Arduino.h>
#include <unity.h>
#include <Display.h>
#include <Touch.h>
#include <SdCard.h>
#include <esp_heap_caps.h>

// Each suite is a separate program. These fixtures outlive Unity's assertion
// longjmp so tearDown can close files and release peripherals after failures.
static Display display;
static Touch touch;
static SdCard card;
static File test_file;
static char temporary_path[64] = {};
static bool temporary_owned = false;
static lv_obj_t *instructions = nullptr;

static void service(uint32_t duration = 5)
{
    const uint32_t start = millis();
    do { lv_timer_handler(); delay(5); } while (uint32_t(millis()-start) < duration);
}

static lv_obj_t *box(int x, int y, int w, int h, uint32_t color)
{
    auto *o=lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(o); lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0); lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_clickable(o,false); lv_obj_set_scrollable(o,false);
    return o;
}
static void animate(void *object, int32_t value) { lv_obj_set_x(static_cast<lv_obj_t *>(object),value); }
static void pattern()
{
    auto *screen=lv_screen_active();
    lv_obj_set_style_bg_color(screen,lv_color_hex(0x0b1220),0); lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
    lv_obj_set_scrollable(screen,false);
    const uint32_t colors[]={0xff0000,0x00ff00,0x0000ff,0xffffff,0,0x808080};
    for (int i=0;i<6;++i) box(30+i*125,65,110,70,colors[i]);
    for (int i=0;i<3;++i) {
        auto *r=box(30,155+i*25,740,18,0);
        lv_obj_set_style_bg_grad_color(r,lv_color_hex(colors[i]),0); lv_obj_set_style_bg_grad_dir(r,LV_GRAD_DIR_HOR,0);
    }
    const char *names[]={"TL 0,0","TR 799,0","BL 0,479","BR 799,479"};
    const lv_align_t aligns[]={LV_ALIGN_TOP_LEFT,LV_ALIGN_TOP_RIGHT,LV_ALIGN_BOTTOM_LEFT,LV_ALIGN_BOTTOM_RIGHT};
    for (int i=0;i<4;++i) { auto *l=lv_label_create(screen); lv_label_set_text(l,names[i]); lv_obj_set_style_text_color(l,lv_color_white(),0); lv_obj_align(l,aligns[i],i%2?-8:8,i<2?8:-8); }
    auto *edge=box(0,0,800,480,0); lv_obj_set_style_bg_opa(edge,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(edge,1,0); lv_obj_set_style_border_color(edge,lv_color_white(),0);
    auto *moving=box(30,395,40,18,0x38bdf8);
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a,moving); lv_anim_set_exec_cb(&a,animate);
    lv_anim_set_values(&a,30,730); lv_anim_set_duration(&a,2000); lv_anim_set_reverse_duration(&a,2000); lv_anim_set_repeat_count(&a,LV_ANIM_REPEAT_INFINITE); lv_anim_start(&a);
    instructions=lv_label_create(screen); lv_obj_set_width(instructions,740); lv_obj_set_pos(instructions,30,300);
    lv_obj_set_style_text_color(instructions,lv_color_white(),0);
    lv_label_set_text(instructions,"Inspect colors, gradients, all four edges and moving bar.\nRecord visual results separately from automatic tests.");
}
static void start_display()
{
    TEST_ASSERT_TRUE_MESSAGE(display.begin(),"RGB/LVGL initialization failed"); pattern();
    TEST_ASSERT_TRUE_MESSAGE(display.show(),"First frame failed; backlight remains disabled");
}
static void start_touch()
{
    TEST_ASSERT_TRUE_MESSAGE(touch.begin(display.handle()),"GT911 not available");
}
static void assert_frame_progress(uint32_t duration=1000)
{
    uint32_t before=display.frame_count(); service(duration);
    TEST_ASSERT_TRUE(display.healthy()); TEST_ASSERT_GREATER_THAN(0,uint32_t(display.frame_count()-before));
}
static void telemetry(uint32_t elapsed, uint32_t frames)
{
    Serial.printf("elapsed_ms=%lu frames=%lu free_internal=%u largest_internal=%u psram_free=%u touch_errors=%lu\n",
        (unsigned long)elapsed,(unsigned long)frames,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)ESP.getFreePsram(),(unsigned long)touch.state().errors);
}
// Bounded chunks allow LVGL/touch service between filesystem calls. No format.
// Return errors to the caller so Unity's tearDown always owns outstanding files.
static const char *verify_sd_file()
{
    uint8_t expected[512], actual[512];
    if (temporary_owned) return "Previous test file still owned";
    bool selected=false;
    for (unsigned i=0;i<1000;++i) {
        snprintf(temporary_path,sizeof(temporary_path),"/pio-test-%08lx-%u.bin",(unsigned long)millis(),i);
        if (!card.exists(temporary_path)) { selected=true; break; }
    }
    if (!selected) return "No unused temporary filename";
    test_file=card.open(temporary_path,FILE_WRITE);
    if (!test_file) return "Temporary file create failed";
    temporary_owned=true;
    for (unsigned chunk=0;chunk<16;++chunk) {
        for (unsigned i=0;i<sizeof(expected);++i) expected[i]=uint8_t(i*37+chunk*19);
        if (test_file.write(expected,sizeof(expected))!=sizeof(expected)) return "Short SD write";
        service();
    }
    test_file.close();
    test_file=card.open(temporary_path,FILE_READ);
    if (!test_file || test_file.size()!=8192) return "Reopen or file length failed";
    for (unsigned chunk=0;chunk<16;++chunk) {
        for (unsigned i=0;i<sizeof(expected);++i) expected[i]=uint8_t(i*37+chunk*19);
        if (test_file.read(actual,sizeof(actual))!=sizeof(actual)) return "Short SD read";
        if (memcmp(expected,actual,sizeof(actual))) return "SD content mismatch";
        service();
    }
    test_file.close();
    if (!card.remove(temporary_path)) return "Temporary file removal failed";
    temporary_owned=false;
    if (card.exists(temporary_path)) return "Temporary file still exists";
    return nullptr;
}
static void cleanup_hardware()
{
    test_file.close();
    bool removed=!temporary_owned || card.remove(temporary_path);
    if (removed) temporary_owned=false;
    else Serial.printf("CLEANUP FAILED: retained test file %s\n",temporary_path);
    card.end(); touch.end(); display.end(); instructions=nullptr;
    TEST_ASSERT_TRUE_MESSAGE(removed,"Could not remove owned temporary file; see path above");
}
static void test_banner()
{
    Serial.begin(115200); delay(2000);
    Serial.printf("Peripheral tests built %s %s; LVGL %d.%d.%d; PCLK %lu\n",__DATE__,__TIME__,lv_version_major(),lv_version_minor(),lv_version_patch(),(unsigned long)Display::kPixelClockHz);
}

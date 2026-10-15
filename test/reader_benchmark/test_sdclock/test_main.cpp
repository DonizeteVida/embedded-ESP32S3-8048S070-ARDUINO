#include <Display.h>
#include <Touch.h>
#include <SdCard.h>
#include "support/Hardware.h"
static uint8_t data[4096],actual[4096];
static void portrait_pattern() {
    auto *screen=lv_screen_active();lv_obj_set_scrollable(screen,false);
    lv_obj_set_style_bg_color(screen,lv_color_hex(0x0b1220),0);
    for(int i=0;i<3;++i) { auto *r=box(12,100+i*90,456,70,0);lv_obj_set_style_bg_grad_color(r,lv_color_hex(i==0?0xff0000:i==1?0x00ff00:0x0000ff),0);lv_obj_set_style_bg_grad_dir(r,LV_GRAD_DIR_HOR,0); }
    auto *edge=box(0,0,480,800,0);lv_obj_set_style_bg_opa(edge,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(edge,2,0);lv_obj_set_style_border_color(edge,lv_color_white(),0);
    instructions=lv_label_create(screen);lv_obj_set_pos(instructions,20,440);lv_obj_set_width(instructions,440);lv_obj_set_style_text_color(instructions,lv_color_white(),0);
    auto *moving=box(20,730,32,24,0x38bdf8);lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,moving);lv_anim_set_exec_cb(&a,animate);lv_anim_set_values(&a,20,428);lv_anim_set_duration(&a,1000);lv_anim_set_reverse_duration(&a,1000);lv_anim_set_repeat_count(&a,LV_ANIM_REPEAT_INFINITE);lv_anim_start(&a);
}
void setUp() { TEST_ASSERT_TRUE(display.begin(LV_DISPLAY_ROTATION_90));portrait_pattern();TEST_ASSERT_TRUE(display.show());start_touch(); }
void tearDown() { cleanup_hardware(); }
static void test_sd_clock_integrity_with_portrait_scanout() {
    const uint32_t speeds[]={1000000,4000000,8000000,16000000};
    for(uint32_t speed:speeds) {
        card.end();bool mounted=card.begin(speed);Serial.printf("SD CLOCK %lu Hz mounted=%u\n",(unsigned long)speed,mounted);
        if(!mounted) { Serial.printf("SD CLOCK REJECTED %lu: mount failed\n",(unsigned long)speed);continue; }
        lv_label_set_text_fmt(instructions,"SD benchmark: %lu MHz\nInspect gradients, edges, animation.\nTap/drag to exercise touch.\nEach speed runs for 20 seconds.",(unsigned long)(speed/1000000));
        uint32_t start=millis(),frames=display.frame_count();unsigned cycles=0;uint64_t bytes=0;bool ok=true;
        for(unsigned i=0;i<1000;++i) { snprintf(temporary_path,sizeof(temporary_path),"/reader-benchmark-%08lx-%u.bin",(unsigned long)millis(),i);if(!card.exists(temporary_path)) break; }
        TEST_ASSERT_FALSE(card.exists(temporary_path));
        while(uint32_t(millis()-start)<20000 && ok) {
            test_file=card.open(temporary_path,FILE_WRITE);temporary_owned=bool(test_file);ok=temporary_owned;
            for(unsigned chunk=0;chunk<8 && ok;++chunk) {
                for(unsigned i=0;i<sizeof(data);++i) data[i]=uint8_t(i*37+chunk*19+cycles);
                ok=test_file.write(data,sizeof(data))==sizeof(data);service(1);bytes+=sizeof(data);
            }
            test_file.close();test_file=card.open(temporary_path);ok=ok && test_file && test_file.size()==32768;
            for(unsigned chunk=0;chunk<8 && ok;++chunk) {
                for(unsigned i=0;i<sizeof(data);++i) data[i]=uint8_t(i*37+chunk*19+cycles);
                ok=test_file.read(actual,sizeof(actual))==sizeof(actual) && !memcmp(data,actual,sizeof(data));service(1);bytes+=sizeof(data);
            }
            test_file.close();if(temporary_owned && card.remove(temporary_path)) temporary_owned=false;else ok=false;
            ok=ok && display.healthy() && touch.state().errors==0;++cycles;
        }
        double hz=double(uint32_t(display.frame_count()-frames))*1000/uint32_t(millis()-start);
        Serial.printf("SD CLOCK RESULT %lu: %s cycles=%u combined_KiB_s=%.1f frame_hz=%.2f internal=%u psram=%u touch_errors=%lu\n",(unsigned long)speed,ok && hz>25?"PASS":"FAIL",cycles,double(bytes)*1000/1024/uint32_t(millis()-start),hz,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),(unsigned)ESP.getFreePsram(),(unsigned long)touch.state().errors);
        TEST_ASSERT_FALSE_MESSAGE(temporary_owned,"Benchmark file cleanup failed");
        if(speed==1000000) TEST_ASSERT_TRUE_MESSAGE(ok && hz>25,"Baseline SD clock failed");
    }
}
void setup() { test_banner();UNITY_BEGIN();RUN_TEST(test_sd_clock_integrity_with_portrait_scanout);UNITY_END(); }
void loop() { delay(100); }

#include <Display.h>
#include <Touch.h>
#include <SdCard.h>
#include "support/Hardware.h"
static lv_obj_t *target=nullptr;
void setUp() {
    TEST_ASSERT_TRUE(display.begin(LV_DISPLAY_ROTATION_90));start_touch();
    auto *screen=lv_screen_active();lv_obj_set_style_bg_color(screen,lv_color_hex(0x0b1220),0);lv_obj_set_scrollable(screen,false);
    instructions=lv_label_create(screen);lv_obj_set_pos(instructions,35,320);lv_obj_set_width(instructions,410);lv_obj_set_style_text_color(instructions,lv_color_white(),0);
    lv_label_set_text(instructions,"Portrait touch alignment\nTap and release each white target.\nTurn board clockwise.");
    target=box(0,0,40,40,0xffffff);TEST_ASSERT_TRUE(display.show());
}
void tearDown() { cleanup_hardware();target=nullptr; }
static void test_portrait_corners_and_center() {
    const int points[][2]={{35,35},{444,35},{35,764},{444,764},{240,400}};
    for(unsigned i=0;i<5;++i) {
        lv_obj_set_pos(target,points[i][0]-20,points[i][1]-20);
        Serial.printf("PORTRAIT TARGET %u: x=%d y=%d; tap then release within 30 seconds\n",i+1,points[i][0],points[i][1]);
        uint32_t start=millis();bool pressed=false,released=false;
        while(uint32_t(millis()-start)<30000 && !released) {
            service();auto s=touch.state();
            if(s.pressed && abs(int(s.x)-points[i][0])<65 && abs(int(s.y)-points[i][1])<65) pressed=true;
            if(pressed && !s.pressed) released=true;
        }
        if(!released) TEST_IGNORE_MESSAGE("Portrait target unfinished; physical alignment remains incomplete");
    }
    TEST_ASSERT_EQUAL(0,touch.state().errors);assert_frame_progress();
}
void setup() { test_banner();UNITY_BEGIN();RUN_TEST(test_portrait_corners_and_center);UNITY_END(); }
void loop() { delay(100); }

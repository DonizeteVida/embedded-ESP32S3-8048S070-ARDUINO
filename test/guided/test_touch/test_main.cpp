#include <Display.h>
#include <Touch.h>
#include <SdCard.h>
#include "support/Hardware.h"
static lv_obj_t *target=nullptr;
void setUp() { start_display(); start_touch(); target=box(0,0,30,30,0xffb020); }
void tearDown() { target=nullptr; cleanup_hardware(); }
static void prompt(const char *text) { lv_label_set_text(instructions,text); Serial.println(text); }
static void healthy_input() { TEST_ASSERT_TRUE(display.healthy()); TEST_ASSERT_EQUAL(0,touch.state().errors); }
static bool wait_release() {
    uint32_t start=millis();
    while (uint32_t(millis()-start)<30000) { service(); healthy_input(); if (!touch.state().pressed) return true; }
    return false;
}
static bool near(const Touch::State &s,int x,int y) { return abs(int(s.x)-x)<=35 && abs(int(s.y)-y)<=35; }
void test_corner_and_center_press_release() {
    const int positions[][2]={{35,35},{764,35},{35,444},{764,444},{400,240}};
    for (auto &p:positions) {
        lv_obj_set_pos(target,p[0]-15,p[1]-15);
        char text[100]; snprintf(text,sizeof(text),"Tap orange target at %d,%d, then release (30 s).",p[0],p[1]); prompt(text);
        if (!wait_release()) TEST_IGNORE_MESSAGE("Incomplete: release not observed");
        uint32_t start=millis(); bool pressed=false;
        while (uint32_t(millis()-start)<30000) { service(); healthy_input(); auto s=touch.state(); if (s.pressed && near(s,p[0],p[1])) { pressed=true; break; } }
        if (!pressed) TEST_IGNORE_MESSAGE("Incomplete: target press not observed; check orientation");
        if (!wait_release()) TEST_IGNORE_MESSAGE("Incomplete: target release not observed");
    }
}
void test_drag_and_release() {
    lv_obj_set_pos(target,65,225); prompt("Hold orange target at 80,240 and drag to 720,240; release there (30 s). If interrupted, retry from the left.");
    if (!wait_release()) TEST_IGNORE_MESSAGE("Incomplete: initial release");
    uint32_t start=millis(); bool started=false,reached=false,completed=false;
    unsigned samples=0,interruptions=0; uint8_t id=0xff;
    while (uint32_t(millis()-start)<30000) {
        service(); healthy_input(); auto s=touch.state();
        if (!started && s.pressed && near(s,80,240)) {
            started=true; id=s.contact_id; samples=0; lv_obj_set_pos(target,705,225);
        }
        if (started) {
            if (!s.pressed) {
                if (reached) { completed=true; break; }
                ++interruptions; started=false; lv_obj_set_pos(target,65,225);
                prompt("Drag interrupted. Lift, then hold LEFT target and slide to RIGHT without lifting. Retry within this window.");
                continue;
            }
            TEST_ASSERT_EQUAL_MESSAGE(id,s.contact_id,"Contact ID changed during drag");
            ++samples;
            if (near(s,720,240)) reached=true;
        }
    }
    Serial.printf("Drag interrupted attempts: %u\n",interruptions);
    if (!started && !interruptions) TEST_IGNORE_MESSAGE("Incomplete: drag not started");
    TEST_ASSERT_TRUE_MESSAGE(completed,"No continuous drag to destination and release within 30 seconds");
    TEST_ASSERT_GREATER_OR_EQUAL(5,samples);
}
void test_two_finger_contact_continuity() {
    lv_obj_set_pos(target,185,225);
    box(585,225,30,30,0x38bdf8);
    prompt("Hold ORANGE at 200,240. Add second finger at BLUE 600,240; lift BLUE only, then ORANGE (30 s).");
    if (!wait_release()) TEST_IGNORE_MESSAGE("Incomplete: initial release");
    uint32_t start=millis(); int id=-1,x=0,y=0; bool multiple=false,single_again=false,released=false;
    while (uint32_t(millis()-start)<30000) {
        service(); healthy_input(); auto s=touch.state();
        if (id<0 && s.pressed && s.contacts==1 && near(s,200,240)) { id=s.contact_id; x=s.x; y=s.y; }
        if (id>=0 && s.pressed) {
            TEST_ASSERT_EQUAL_MESSAGE(id,s.contact_id,"Selected contact changed during hold");
            TEST_ASSERT_TRUE_MESSAGE(abs(int(s.x)-x)<=50 && abs(int(s.y)-y)<=50,"Held contact jumped");
            if (s.contacts>=2) multiple=true;
            if (multiple && s.contacts==1) single_again=true;
        }
        if (id>=0 && !s.pressed) { released=true; break; }
    }
    if (!multiple || !single_again || !released) TEST_IGNORE_MESSAGE("Incomplete: two-finger sequence");
}
void setup() {
    test_banner(); UNITY_BEGIN(); RUN_TEST(test_corner_and_center_press_release);
    RUN_TEST(test_drag_and_release); RUN_TEST(test_two_finger_contact_continuity); UNITY_END();
}
void loop() { delay(100); }

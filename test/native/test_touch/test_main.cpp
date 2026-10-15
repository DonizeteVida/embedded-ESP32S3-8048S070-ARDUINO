#include <unity.h>
#include <Display.h>
#include <Touch.h>
static Display display;
static Touch touch;
void setUp() { touch.end(); display.end(); fake::reset_core(); fake::reset_lcd(); fake::reset_wire(); TEST_ASSERT_TRUE(display.begin()); }
void tearDown() { touch.end(); display.end(); TEST_ASSERT_EQUAL(fake::wire_acquisitions,fake::wire_ends); }
static void poll() { lv_indev_read(lv_indev_get_next(nullptr)); }
static void detection_and_lifecycle() {
    TEST_ASSERT_FALSE(touch.begin(nullptr)); TEST_ASSERT_TRUE(touch.begin(display.handle()));
    TEST_ASSERT_EQUAL(0x5d,touch.device_address()); TEST_ASSERT_EQUAL(800,touch.raw_width_value()); TEST_ASSERT_EQUAL(480,touch.raw_height_value());
    TEST_ASSERT_TRUE(touch.begin(display.handle())); TEST_ASSERT_EQUAL(1,fake::wire_begins);
    Touch other; TEST_ASSERT_FALSE(other.begin(display.handle())); other.end(); TEST_ASSERT_TRUE(touch.state().available);
    touch.end(); TEST_ASSERT_FALSE(touch.state().available); TEST_ASSERT_NULL(lv_indev_get_next(nullptr));
    fake::device_address=0x14; TEST_ASSERT_TRUE(touch.begin(display.handle())); TEST_ASSERT_EQUAL(0x14,touch.device_address());
    for (auto &w:fake::writes) { TEST_ASSERT_EQUAL_HEX16(0x814e,w.first); TEST_ASSERT_EQUAL(0,w.second[0]); }
}
static void fallback_and_invalid_configuration() {
    fake::reset_detect=true; TEST_ASSERT_TRUE(touch.begin(display.handle()));
    TEST_ASSERT_GREATER_OR_EQUAL(115,millis()); TEST_ASSERT_EQUAL(INPUT,fake::modes[18]); TEST_ASSERT_EQUAL(HIGH,fake::pins[38]); touch.end();
    fake::registers[0x8048]={1,0,0xe0,1}; TEST_ASSERT_FALSE(touch.begin(display.handle())); TEST_ASSERT_FALSE(touch.state().available);
    fake::registers[0x8048]={0x20,3,0xe0,1}; fake::ack_error=true; TEST_ASSERT_FALSE(touch.begin(display.handle()));
    fake::ack_error=false; fake::short_register=0x8048; TEST_ASSERT_FALSE(touch.begin(display.handle()));
    fake::short_register=0; fake::wire_short=true; TEST_ASSERT_FALSE(touch.begin(display.handle()));
    TEST_ASSERT_EQUAL(INPUT,fake::modes[18]);
}
static void initialization_bus_and_product_errors() {
    fake::wire_ok=false; TEST_ASSERT_FALSE(touch.begin(display.handle())); fake::wire_ok=true;
    fake::registers[0x8140]={'X','1','1',0}; TEST_ASSERT_FALSE(touch.begin(display.handle()));
    TEST_ASSERT_FALSE(touch.state().available); TEST_ASSERT_NULL(lv_indev_get_next(nullptr));
    fake::registers[0x8140]={'9','1','1',0}; fake::registers[0x8048]={0,0x20,0xe0,1}; TEST_ASSERT_FALSE(touch.begin(display.handle()));
}
static void mapping_tracking_and_release() {
    fake::registers[0x8048]={0x40,6,0xc0,3}; // 1600x960 scales endpoints exactly.
    TEST_ASSERT_TRUE(touch.begin(display.handle()));
    fake::report({{7,1599,959}}); poll(); auto s=touch.state();
    TEST_ASSERT_TRUE(s.pressed); TEST_ASSERT_EQUAL(799,s.x); TEST_ASSERT_EQUAL(479,s.y); TEST_ASSERT_EQUAL(1,s.presses);
    fake::report({{3,100,100},{7,800,480}}); poll(); s=touch.state();
    TEST_ASSERT_EQUAL(7,s.contact_id); TEST_ASSERT_EQUAL(2,s.contacts); TEST_ASSERT_EQUAL(399,s.x); TEST_ASSERT_EQUAL(239,s.y); TEST_ASSERT_EQUAL(1,s.presses);
    fake::report({{3,100,100}}); poll(); TEST_ASSERT_FALSE(touch.state().pressed);
    fake::report({{3,0,0}}); poll(); TEST_ASSERT_EQUAL(2,touch.state().presses); TEST_ASSERT_EQUAL(0,touch.state().x);
    fake::report({}); poll(); TEST_ASSERT_FALSE(touch.state().pressed);
}
static void malformed_reports_release_and_acknowledge() {
    TEST_ASSERT_TRUE(touch.begin(display.handle()));
    fake::report({{1,10,10}}); poll(); TEST_ASSERT_TRUE(touch.state().pressed);
    unsigned acks=fake::acks; fake::registers[0x814e]={0x86}; poll();
    TEST_ASSERT_EQUAL(acks+1,fake::acks); TEST_ASSERT_FALSE(touch.state().pressed); TEST_ASSERT_EQUAL(1,touch.state().errors);
    fake::report({{1,800,10}}); poll(); TEST_ASSERT_EQUAL(2,touch.state().errors);
    fake::report({{1,10,480}}); poll(); TEST_ASSERT_EQUAL(3,touch.state().errors);
    fake::report({{1,10,10}}); fake::registers[0x814f].pop_back(); poll(); TEST_ASSERT_EQUAL(4,touch.state().errors);
    fake::report({{1,10,10}}); poll(); fake::ack_error=true; fake::report({}); poll();
    TEST_ASSERT_FALSE(touch.state().pressed); TEST_ASSERT_EQUAL(5,touch.state().errors);
    fake::ack_error=false; fake::wire_read_error=true; poll(); TEST_ASSERT_EQUAL(6,touch.state().errors);
}
static void stale_release_and_wraparound() {
    TEST_ASSERT_TRUE(touch.begin(display.handle())); fake::now=UINT32_MAX-100;
    fake::report({{1,20,30}}); poll(); auto acks=fake::acks;
    fake::now+=149; poll(); TEST_ASSERT_TRUE(touch.state().pressed); TEST_ASSERT_EQUAL(acks,fake::acks);
    fake::now+=1; poll(); TEST_ASSERT_FALSE(touch.state().pressed); TEST_ASSERT_EQUAL(0,touch.state().errors);
    fake::report({{1,20,30}}); poll(); fake::wire_short=true; poll(); TEST_ASSERT_FALSE(touch.state().pressed);
}
int main() {
    UNITY_BEGIN(); RUN_TEST(detection_and_lifecycle); RUN_TEST(fallback_and_invalid_configuration);
    RUN_TEST(initialization_bus_and_product_errors); RUN_TEST(mapping_tracking_and_release);
    RUN_TEST(malformed_reports_release_and_acknowledge); RUN_TEST(stale_release_and_wraparound);
    return UNITY_END();
}

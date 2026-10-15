#include <Display.h>
#include <Touch.h>
#include <SdCard.h>
#include "support/Hardware.h"
void setUp() { start_display(); }
void tearDown() { cleanup_hardware(); }
void test_gt911_detection_and_polling() {
    start_touch(); TEST_ASSERT_TRUE(touch.device_address()==0x5d || touch.device_address()==0x14);
    TEST_ASSERT_GREATER_OR_EQUAL(2,touch.raw_width_value()); TEST_ASSERT_GREATER_OR_EQUAL(2,touch.raw_height_value());
    service(2000); TEST_ASSERT_TRUE(touch.state().available); TEST_ASSERT_EQUAL(0,touch.state().errors);
    assert_frame_progress();
}
void setup() { test_banner(); UNITY_BEGIN(); RUN_TEST(test_gt911_detection_and_polling); UNITY_END(); }
void loop() { delay(100); }

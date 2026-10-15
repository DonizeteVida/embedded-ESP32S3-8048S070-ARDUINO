#pragma once
#include "Hardware.h"
void setUp() { start_display(); start_touch(); }
void tearDown() { cleanup_hardware(); }
void test_expected_mount_failure_keeps_ui_usable() {
    TEST_ASSERT_FALSE_MESSAGE(card.begin(),"Expected mount failure; check required card fixture");
    TEST_ASSERT_FALSE(card.is_mounted()); TEST_ASSERT_FALSE(card.open("/"));
    assert_frame_progress(5000); TEST_ASSERT_TRUE(touch.state().available); TEST_ASSERT_EQUAL(0,touch.state().errors);
}
void setup() { test_banner(); UNITY_BEGIN(); RUN_TEST(test_expected_mount_failure_keeps_ui_usable); UNITY_END(); }
void loop() { delay(100); }

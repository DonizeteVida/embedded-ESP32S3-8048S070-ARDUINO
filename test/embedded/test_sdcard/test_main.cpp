#include <Display.h>
#include <Touch.h>
#include <SdCard.h>
#include "support/Hardware.h"
void setUp() { start_display(); start_touch(); }
void tearDown() { cleanup_hardware(); }
void test_sd_mount_write_read_remove() {
    TEST_ASSERT_TRUE_MESSAGE(card.begin(),"Insert a FAT16/FAT32 card before boot");
    const char *error=verify_sd_file(); TEST_ASSERT_NULL_MESSAGE(error,error?error:"");
    assert_frame_progress(); TEST_ASSERT_EQUAL(0,touch.state().errors);
}
void setup() { test_banner(); UNITY_BEGIN(); RUN_TEST(test_sd_mount_write_read_remove); UNITY_END(); }
void loop() { delay(100); }

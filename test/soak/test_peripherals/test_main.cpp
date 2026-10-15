#include <Display.h>
#include <Touch.h>
#include <SdCard.h>
#include "support/Hardware.h"
void setUp() { start_display(); start_touch(); }
void tearDown() { cleanup_hardware(); }
void test_one_minute_combined_workload() {
    TEST_ASSERT_TRUE_MESSAGE(card.begin(),"Soak requires a FAT16/FAT32 card");
    lv_label_set_text(instructions,"1 minute soak: inspect animation and use touch.\nSD data verified every second. Record visual results separately.");
    uint32_t start=millis(),last_io=start-1000,last_log=start,last_frames=display.frame_count();
    unsigned cycles=0;
    while (uint32_t(millis()-start)<60000) {
        service(); TEST_ASSERT_TRUE(display.healthy()); TEST_ASSERT_EQUAL(0,touch.state().errors);
        uint32_t now=millis();
        if (uint32_t(now-last_io)>=1000) {
            last_io=now; const char *error=verify_sd_file(); TEST_ASSERT_NULL_MESSAGE(error,error?error:""); ++cycles;
        }
        now=millis();
        if (uint32_t(now-last_log)>=5000) {
            uint32_t frames=display.frame_count();
            double hz=double(uint32_t(frames-last_frames))*1000/uint32_t(now-last_log);
            telemetry(now-start,frames); Serial.printf("frame_hz=%.2f sd_cycles=%u\n",hz,cycles);
            TEST_ASSERT_TRUE_MESSAGE(hz>25.0 && hz<33.0,"Frame rate outside 16 MHz baseline tolerance");
            last_frames=frames; last_log=now;
        }
    }
    TEST_ASSERT_GREATER_OR_EQUAL(50,cycles); TEST_ASSERT_FALSE(temporary_owned);
}
void setup() { test_banner(); UNITY_BEGIN(); RUN_TEST(test_one_minute_combined_workload); UNITY_END(); }
void loop() { delay(100); }

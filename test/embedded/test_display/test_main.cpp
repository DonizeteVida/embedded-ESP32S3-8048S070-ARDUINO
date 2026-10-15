#include <Display.h>
#include <Touch.h>
#include <SdCard.h>
#include "support/Hardware.h"
void setUp() { start_display(); }
void tearDown() { cleanup_hardware(); }
void test_display_scanout_and_rendering() { assert_frame_progress(5000); telemetry(5000,display.frame_count()); }
void setup() { test_banner(); UNITY_BEGIN(); RUN_TEST(test_display_scanout_and_rendering); UNITY_END(); }
void loop() { delay(100); }

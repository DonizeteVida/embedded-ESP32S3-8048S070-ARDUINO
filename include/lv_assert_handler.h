#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void display_assert_failed(void);

#ifdef __cplusplus
}
#endif

// LVGL inserts this macro as a complete statement, including its semicolon.
#define LV_ASSERT_HANDLER display_assert_failed();

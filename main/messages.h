#pragma once

#include "lvgl.h"

// Initialize on the persistent dashboard screen while the caller holds
// the LVGL display lock. Call once from DisplayDashboardInitialize().
void MessagesInit(lv_obj_t* screen);

// Handle the S3 UART-wrapped LINE JSON. Acquires/releases the LVGL lock.
void MessagesHandle(const char* json);

// Clear the visible message history. Acquires/releases the LVGL lock.
void MessagesClear();

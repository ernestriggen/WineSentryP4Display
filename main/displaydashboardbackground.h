// displaydashboardbackground.h

#pragma once

#include "lvgl.h"

// Create the persistent dashboard background.
// Caller must hold the LVGL display lock.
// SPIFFS must already be mounted.
bool DisplayDashboardBackgroundInitialize(lv_obj_t* screen);

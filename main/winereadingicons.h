// winereadingicons.h

#pragma once

#include <lvgl.h>

// Attach the thermometer and humidity icons to the dashboard.
// Caller must hold the BSP display lock.
// SPIFFS must already be mounted.
bool WineReadingIconsAttach(lv_obj_t* screen);

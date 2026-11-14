
#pragma once

#include "lvgl.h"

// Initialize connectivity icons on the dashboard.
// Caller must hold the LVGL display lock.
bool LowerStatusInit(lv_obj_t* screen);

// Update from sensor STATUSBAR.
//
// wifi and mqtt:
//     "Y"     = connected
//     "N"     = disconnected
//     nullptr = unchanged
//
// hasRSSI indicates whether the JSON supplied a
// valid RSSI measurement.
//
// Caller must hold the LVGL display lock.
void LowerStatusSetConnectivity(
    const char* wifi,
    const char* mqtt,
    bool hasRSSI,
    int rssi
);

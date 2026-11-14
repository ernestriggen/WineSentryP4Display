// rightstatus.h 

#pragma once

#include "lvgl.h"

// Create the right-side status display.
//
// Must be called with the LVGL display lock held.
bool RightStatusInit(lv_obj_t* screen);

// Update the power source.
//
// Expected values include "BATTERY" and "AC".
// Missing or unrecognized values preserve the previous state.
void RightStatusSetPowerSource(const char* powerSource);

// Update battery percentage (0 to 100).
// Invalid values are ignored.
void RightStatusSetBatteryPercent(int percent);

// Update sensitivity.
// Negative values represent unavailable data.
void RightStatusSetSensitivity(int sensitivity);

void RightStatusRender();

// wineglass.h

#pragma once

#include "lvgl.h"
#include "models/winesentrystatemachine.h"

// Call once when creating the dashboard.
// Caller must hold the LVGL display lock.
bool WineGlassInitialize(lv_obj_t* screen);

// Switch the displayed wineglass according to WINESTATE.
// Caller must hold the LVGL display lock.
void WineGlassShow(WINESTATE state);
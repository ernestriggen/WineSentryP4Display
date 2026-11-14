// ping.h

#pragma once

#include "lvgl.h"

// Create the PING indicator on the dashboard.
// Must be called while holding the LVGL display lock.
bool PingInit(lv_obj_t* screen);

// Called when a PING message arrives.
// goingToSleep = true displays the moon.
// goingToSleep = false alternates the gold/faded heart.
void PingHandle(bool goingToSleep);

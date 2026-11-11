// statusbar.h

#pragma once

//#include <stdbool.h>
#include <lvgl.h>

// Create the temporary STATUSBAR display.
void StatusBarInit(lv_obj_t* screen);

// Receive and display a STATUSBAR JSON payload.
void StatusBarHandle(const char* json);
// Update the LuxSense ID from an incoming WINE_READING.
void setStatusBarLuxSenseID(const char* luxSenseID);
// Update the sensor model from an incoming WINE_READING.
void setStatusBarSensorModel(const char* sensorModel);

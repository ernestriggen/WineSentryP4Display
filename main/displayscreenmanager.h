
#pragma once

#include "lvgl.h"

enum class WineSentryScreen
    {
    DASHBOARD,
    LABEL_RECOGNITION
    };

void DisplayScreenManagerInitialize();

bool DisplayScreenShow(WineSentryScreen screen);

WineSentryScreen DisplayScreenGetCurrent();

// displaywinedetailscreen.h
#pragma once

#include <cstddef>
#include <cstdint>

struct WineDetailsView
{
    const char* producer = "";
    const char* wineName = "";
    const char* cuvee = "";
    const char* vintage = "";
    const char* rating = "";
    const char* reportedValue = "";

    const char* aromas[5] = {};
    const char* palate[5] = {};
};

// All display functions require the LVGL lock.
void WineDetailsScreenShow(const WineDetailsView& wine);
void WineDetailsScreenDismiss();
void WineDetailsScreenApplyLabel();

// JPEG hardware decoding only. Must be called WITHOUT the LVGL lock.
// On success, ApplyLabel() transfers the prepared pixels to the display.
bool WineDetailsScreenPrepareLabel(const uint8_t* jpeg, size_t jpegSize);

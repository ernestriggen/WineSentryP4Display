// arc_text.h

#pragma once

#include <lvgl.h>
#include <cstddef>

// Small, reusable curved-text renderer for the 800 x 800 circular display.
// Keep instances alive as long as their parent LVGL screen exists.
// Caller must hold the LVGL display lock for every call.
static constexpr size_t ARC_TEXT_MAX_CHARS = 32;

struct ArcText
{
    lv_obj_t* characters[ARC_TEXT_MAX_CHARS] = {};
    size_t count = 0;
    int centerX = 400;
    int centerY = 400;
    int radius = 382;
    float startDegrees = 202.0f;
    float endDegrees = 181.0f;
    const lv_font_t* font = nullptr;
};

void ArcTextInit(
    ArcText& arc,
    lv_obj_t* parent,
    int centerX,
    int centerY,
    int radius,
    float startDegrees,
    float endDegrees,
    const lv_font_t* font,
    uint32_t textColor);

void ArcTextSetText(ArcText& arc, const char* text);
void ArcTextSetColor(ArcText& arc, uint32_t textColor);

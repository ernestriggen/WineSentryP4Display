// displayhelper.h

#pragma once

#include "lvgl.h"

// Call from an LVGL-safe context (hold bsp_display_lock if outside LVGL task).
// Coordinates are scaled from a 720 x 720 design canvas.
void DisplayDrawFrame(lv_obj_t* screen, lv_color_t accent);
void DisplaySetFrameAccent(lv_color_t accent);

// logoImage may be nullptr; pass an LVGL image descriptor for your crowned cat.
// The helper does not take ownership of logoImage.
void DisplayDrawDecorations(lv_obj_t* screen, const lv_image_dsc_t* logoImage = nullptr);

// Remove cached pointers before deleting/rebuilding the parent screen.
void DisplayHelpersReset();

// ping.cpp

// we will receive PING messages from the world before us.
// this code will produce a beating heart on the display to let
// the user know all is working

#include "ping.h"

#include "bsp/esp-bsp.h"
#include "lvgl.h"

static lv_obj_t* pingLabel = nullptr;
static bool pingVisible = false;

void PingHandle(bool goingToSleep)
  {
  if (bsp_display_lock(1000) != ESP_OK)
    return;

  if (pingLabel == nullptr)
    {
    pingLabel = lv_label_create(lv_screen_active());

    lv_obj_set_style_text_color(
        pingLabel,
        lv_color_hex(0xFFFFFF),
        0);

    lv_obj_set_style_text_font(
        pingLabel,
        &lv_font_montserrat_24,
        0);

    // Upper-right quadrant, away from center.
    lv_obj_align(pingLabel, LV_ALIGN_TOP_RIGHT, -100, 95);
    }

  if (goingToSleep)
    {
    lv_label_set_text(pingLabel, "SLEEP");
    pingVisible = false;
    }
  else
    {
    pingVisible = !pingVisible;

    if (pingVisible)
      lv_label_set_text(pingLabel, "PING");
    else
      lv_label_set_text(pingLabel, "---");
    }

  bsp_display_unlock();
  }
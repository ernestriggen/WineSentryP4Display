// displaydashboardscreen.h

#include <bsp/esp-bsp.h>
#include <bsp/display.h>
#include <esp_log.h>

#include "displaydashboardbackground.h"
#include "displaydashboardscreen.h"
#include "winereading.h"

static const char* TAG = "DASHBOARD";

static lv_obj_t* dashboardScreen = nullptr;


///////////////////////////////////////////////////////////////////////////////////////////
// Create the WineSentry dashboard.
// Called once, after the splash screen has finished.
//
void DisplayDashboardInitialize()
{
    if (dashboardScreen != nullptr)
        return;

    bsp_display_lock(-1);

    dashboardScreen = lv_obj_create(nullptr);

    if (dashboardScreen == nullptr)
    {
        ESP_LOGE(TAG, "Unable to create dashboard screen");
        bsp_display_unlock();
        return;
    }

    lv_obj_set_style_bg_color(
        dashboardScreen,
        lv_color_black(),
        0
    );

    lv_obj_set_style_bg_opa(
        dashboardScreen,
        LV_OPA_COVER,
        0
    );

    lv_obj_set_scrollable(dashboardScreen, false);

    // Create the luxury WineSentry background.
    bool backgroundReady =
        DisplayDashboardBackgroundInitialize(dashboardScreen);

    if (backgroundReady)
    {
        WineReadingAttachScreen(dashboardScreen);
    }
    else
    {
        ESP_LOGE(TAG, "Dashboard background unavailable");
    }
    

    bsp_display_unlock();

    ESP_LOGI(TAG, "Dashboard screen created");
}


///////////////////////////////////////////////////////////////////////////////////////////
// Return the persistent dashboard LVGL screen.
//
lv_obj_t* DisplayDashboardGetScreen()
{
    return dashboardScreen;
}

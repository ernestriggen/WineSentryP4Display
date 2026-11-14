// displaydashboardscreen.h

#include <bsp/esp-bsp.h>
#include <bsp/display.h>
#include <esp_log.h>

#include "displaydashboardbackground.h"
#include "displaydashboardscreen.h"
#include "lowerstatus.h"
#include "messages.h"
#include "ping.h"
#include "rightstatus.h"
#include "statusbar.h"
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
    bool backgroundReady = DisplayDashboardBackgroundInitialize(dashboardScreen);

    if (backgroundReady)
    {
        MessagesInit(dashboardScreen);
        WineReadingAttachScreen(dashboardScreen);
        StatusBarInit(dashboardScreen);
        if (!LowerStatusInit(dashboardScreen))
            {
            ESP_LOGE(TAG, "Lower status initialization failed");
            }
        if (!PingInit(dashboardScreen))
            {
            ESP_LOGE(TAG, "PING indicator initialization failed");
            }            
    }
    else
    {
        ESP_LOGE(TAG, "Dashboard background unavailable");
    }
    
    if (!RightStatusInit(dashboardScreen))
        {
        ESP_LOGE(TAG, "Right status initialization failed");
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

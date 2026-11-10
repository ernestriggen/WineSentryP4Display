// displayscreenmanager.cpp

#include <bsp/display.h>
#include <bsp/esp-bsp.h>
#include <esp_err.h>
#include <esp_log.h>

#include "displaydashboardscreen.h"
#include "displayscreenmanager.h"

static const char* TAG = "SCREEN_MANAGER";

static WineSentryScreen currentScreen = WineSentryScreen::DASHBOARD;
static bool screenManagerInitialized = false;


///////////////////////////////////////////////////////////////////////////////////////////
// Initialize the screen manager and show the normal dashboard.
//
void DisplayScreenManagerInitialize()
    {
    if (screenManagerInitialized)
        return;

    // The dashboard initializes its own LVGL objects and background.
    DisplayDashboardInitialize();

    if (DisplayDashboardGetScreen() == nullptr)
        {
        ESP_LOGE(TAG, "Dashboard initialization failed");
        return;
        }

    if (bsp_display_lock(1000) != ESP_OK)
        {
        ESP_LOGE(TAG, "Unable to acquire LVGL lock");
        return;
        }

    lv_screen_load(DisplayDashboardGetScreen());

    currentScreen = WineSentryScreen::DASHBOARD;
    screenManagerInitialized = true;

    bsp_display_unlock();

    ESP_LOGI(TAG, "Screen manager initialized");
    }


///////////////////////////////////////////////////////////////////////////////////////////
// Switch to the requested screen.
// The LVGL screens remain allocated when they are not active.
//
bool DisplayScreenShow(WineSentryScreen screen)
    {
    if (!screenManagerInitialized)
        {
        ESP_LOGW(TAG, "Screen manager is not initialized");
        return false;
        }

    lv_obj_t* targetScreen = nullptr;

    switch (screen)
        {
        case WineSentryScreen::DASHBOARD:
            {
            targetScreen = DisplayDashboardGetScreen();
            break;
            }

        case WineSentryScreen::LABEL_RECOGNITION:
            {
            // Recognition screen will be connected here later.
            ESP_LOGW(TAG, "Label recognition screen not implemented");
            return false;
            }

        default:
            {
            ESP_LOGW(TAG, "Unknown screen requested");
            return false;
            }
        }

    if (targetScreen == nullptr)
        {
        ESP_LOGW(TAG, "Requested screen is unavailable");
        return false;
        }

    if (bsp_display_lock(1000) != ESP_OK)
        {
        ESP_LOGW(TAG, "Unable to acquire LVGL lock");
        return false;
        }

    if (lv_screen_active() != targetScreen)
        {
        lv_screen_load(targetScreen);
        }

    currentScreen = screen;

    bsp_display_unlock();

    ESP_LOGI(TAG, "Screen changed to %d", static_cast<int>(screen));

    return true;
    }


///////////////////////////////////////////////////////////////////////////////////////////
// Return the currently selected screen.
//
WineSentryScreen DisplayScreenGetCurrent()
    {
    return currentScreen;
    }

// main.cpp

// this is where it all begins.
// really just basic setup and then it all is handled by the statemachine.cpp

#include <bsp/esp-bsp.h>
#include <bsp/display.h>
#include <bsp_board_extra.h>
#include <esp_log.h>

// methods called and defined in this module
static void setup();
static void setupDisplay();

#include "DisplayCommunications.h"
#include "displayscreenmanager.h"
#include "splashscreen.h"

static const char *TAG = "WineSentry";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "WineSentry starting");

    setup();

    ESP_LOGI(TAG, "WineSentry application ready");

    // Next:
    // winesentry::start_state_machine();
}

static void setup()
{
    setupDisplay();

    play_splash_screen();

    DisplayScreenManagerInitialize();

    // Start communications immediately.
    ESP_ERROR_CHECK(InitializeDisplayCommunications());

    xTaskCreate(
        DisplayCommunicationsLoop,
        "DisplayUART",
        4096,
        nullptr,
        5,
        nullptr
    );
}

static void setupDisplay()
{
    bsp_display_cfg_t cfg = {};

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"

    cfg.lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();

    #pragma GCC diagnostic pop

    cfg.rotation = ESP_LV_ADAPTER_ROTATE_0;
    cfg.tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL;

    cfg.touch_flags.swap_xy = 0;
    cfg.touch_flags.mirror_x = 0;
    cfg.touch_flags.mirror_y = 0;

    

    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_off();
}
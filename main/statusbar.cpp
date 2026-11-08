
// statusbar.cpp

// herein lies the code that will unpack a statusbar message and display it on the display

#include <bsp/esp-bsp.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "cJSON.h"
#include "esp_log.h"

#include "statusbar.h"

static const char* TAG = "StatusBar";

static lv_obj_t* statusLabel = nullptr;
static bool statusVisible = true;


void StatusBarInit()
    {
    if (statusLabel != nullptr)
        return;

    lv_obj_t* screen = lv_screen_active();

    statusLabel = lv_label_create(screen);

    // Size and positioning
    lv_obj_set_width(statusLabel, lv_pct(85));
    lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_center(statusLabel);

    // Bright white text on our black display
    lv_obj_set_style_text_color(
        statusLabel,
        lv_color_white(),
        0
        );

    // Readable font (built into LVGL if enabled)
    lv_obj_set_style_text_font(
        statusLabel,
        &lv_font_montserrat_20,
        0
        );

    lv_obj_set_style_text_align(
        statusLabel,
        LV_TEXT_ALIGN_CENTER,
        0
        );

    lv_label_set_text(
        statusLabel,
        "Waiting for WineSentry sensor..."
        );
    }


void StatusBarSetVisible(bool visible)
    {
    if (bsp_display_lock(1000) == ESP_OK)
        {
        statusVisible = visible;

        if (statusLabel != nullptr)
            lv_obj_set_hidden(statusLabel, !visible);

        bsp_display_unlock();
        }
    }


void StatusBarHandle(const char* json)
    {
    if (json == nullptr)
        return;

    cJSON* root = cJSON_Parse(json);

    if (root == nullptr)
        {
        ESP_LOGW(TAG, "Invalid STATUSBAR JSON");
        return;
        }

    const cJSON* payload =
        cJSON_GetObjectItemCaseSensitive(root, "payload");

    const cJSON* data =
        cJSON_GetObjectItemCaseSensitive(payload, "data");

    if (!cJSON_IsString(data))
        {
        ESP_LOGW(TAG, "STATUSBAR payload.data missing or invalid");
        cJSON_Delete(root);
        return;
        }

    cJSON* status = cJSON_Parse(data->valuestring);

    if (status == nullptr)
        {
        ESP_LOGW(TAG, "Invalid STATUSBAR data JSON");
        cJSON_Delete(root);
        return;
        }

    const char* uptime =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(status, "UT"));

    const char* version =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(status, "V"));

    const char* name =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(status, "N"));

    const char* wifi =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(status, "W"));

    const char* mqtt =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(status, "M"));

    const char* power =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(status, "P"));

    const char* connection =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(status, "C"));

    const cJSON* rssiField =
        cJSON_GetObjectItemCaseSensitive(status, "R");

    const cJSON* aField =
        cJSON_GetObjectItemCaseSensitive(status, "A");

    const cJSON* bField =
        cJSON_GetObjectItemCaseSensitive(status, "B");

    const cJSON* sField =
        cJSON_GetObjectItemCaseSensitive(status, "S");

    char text[512];

    snprintf(
        text,
        sizeof(text),
        "WineSentry STATUS\n"
        "\n"
        "Sensor: %s\n"
        "Firmware: %s\n"
        "Uptime: %s\n"
        "WiFi: %s  RSSI: %d\n"
        "MQTT: %s\n"
        "Power: %s\n"
        "Connection: %s\n"
        "A: %d  B: %d  S: %d",
        name ? name : "?",
        version ? version : "?",
        uptime ? uptime : "?",
        wifi ? wifi : "?",
        cJSON_IsNumber(rssiField) ? rssiField->valueint : 0,
        mqtt ? mqtt : "?",
        power ? power : "?",
        connection ? connection : "?",
        cJSON_IsNumber(aField) ? aField->valueint : 0,
        cJSON_IsNumber(bField) ? bField->valueint : 0,
        cJSON_IsNumber(sField) ? sField->valueint : 0
        );

    if (bsp_display_lock(1000) == ESP_OK)
        {
        if (statusLabel == nullptr)
            StatusBarInit();

        if (statusLabel != nullptr && statusVisible)
            lv_label_set_text(statusLabel, text);

        bsp_display_unlock();
        }
    else
        {
        ESP_LOGW(TAG, "Failed to acquire LVGL display lock");
        }

    cJSON_Delete(status);
    cJSON_Delete(root);
    }

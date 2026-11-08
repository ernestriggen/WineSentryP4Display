
#include <bsp/display.h>
#include <bsp/esp-bsp.h>
#include <cJSON.h>
#include <esp_err.h>
#include <esp_log.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "winereading.h"

static const char* TAG = "WINE_READING";

static lv_obj_t* wineReadingLabel = nullptr;

///////////////////////////////////////////////////////////////////////////////////////////
// Extract a string field from a JSON object.
//
static const char* GetReadingString(const cJSON* root, const char* name)
    {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);

    if (cJSON_IsString(item) && item->valuestring != nullptr)
        return item->valuestring;

    return "--";
    }


///////////////////////////////////////////////////////////////////////////////////////////
// Extract an integer field from a JSON object.
//
static int GetReadingInteger(const cJSON* root, const char* name, int fallback = -1)
    {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);

    if (cJSON_IsNumber(item))
        return item->valueint;

    return fallback;
    }


///////////////////////////////////////////////////////////////////////////////////////////
// Receive a complete WINE_READING message from the S3 and display its fields.
//
void WineReadingHandle(const char* json)
    {
    if (json == nullptr)
        return;

    cJSON* envelope = cJSON_Parse(json);

    if (envelope == nullptr)
        {
        ESP_LOGW(TAG, "Invalid WINE_READING envelope");
        return;
        }

    const cJSON* payload =
        cJSON_GetObjectItemCaseSensitive(envelope, "payload");

    const cJSON* data =
        cJSON_GetObjectItemCaseSensitive(payload, "data");

    if (!cJSON_IsString(data) || data->valuestring == nullptr)
        {
        ESP_LOGW(TAG, "WINE_READING payload.data missing");
        cJSON_Delete(envelope);
        return;
        }

    cJSON* reading = cJSON_Parse(data->valuestring);

    if (reading == nullptr)
        {
        ESP_LOGW(TAG, "Invalid wine reading JSON");
        cJSON_Delete(envelope);
        return;
        }

    const char* actualTemp = GetReadingString(reading, "ActualTemp");
    const char* actualHumidity = GetReadingString(reading, "ActualHumidity");
    const char* targetTemp = GetReadingString(reading, "TargetTemp");
    const char* targetHumidity = GetReadingString(reading, "TargetHumidity");
    const char* readingStatus = GetReadingString(reading, "ReadingStatus");
    const char* sensorModel = GetReadingString(reading, "SensorModel");

    int batteryPercent = GetReadingInteger(reading, "BatteryPercent");

    char displayText[512];

    snprintf(
        displayText,
        sizeof(displayText),
        "WINE READING\n"
        "\n"
        "Temp: %s F\n"
        "Target: %s F\n"
        "\n"
        "Humidity: %s%%\n"
        "Target: %s%%\n"
        "\n"
        "Battery: %d%%\n"
        "Status: %s\n"
        "Model: %s",
        actualTemp,
        targetTemp,
        actualHumidity,
        targetHumidity,
        batteryPercent,
        readingStatus,
        sensorModel
        );

    ESP_LOGI(TAG, "Wine reading received: %s", displayText);

    if (bsp_display_lock(1000) == ESP_OK)
        {
        if (wineReadingLabel == nullptr)
            {
            wineReadingLabel = lv_label_create(lv_screen_active());

            lv_obj_set_width(wineReadingLabel, lv_pct(52));
            lv_label_set_long_mode(wineReadingLabel, LV_LABEL_LONG_WRAP);

            lv_obj_set_style_text_font(
                wineReadingLabel,
                &lv_font_montserrat_16,
                0
                );

            lv_obj_set_style_text_color(
                wineReadingLabel,
                lv_color_white(),
                0
                );

            lv_obj_set_style_text_align(
                wineReadingLabel,
                LV_TEXT_ALIGN_LEFT,
                0
                );

            lv_obj_align(
                wineReadingLabel,
                LV_ALIGN_LEFT_MID,
                45,
                0
                );
            }

        lv_label_set_text(wineReadingLabel, displayText);

        bsp_display_unlock();
        }

    cJSON_Delete(reading);
    cJSON_Delete(envelope);
    }

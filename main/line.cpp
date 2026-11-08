
// line.cpp

// Receive LINE messages from the S3 and display them on the P4.

#include <bsp/esp-bsp.h>
#include <stdio.h>
#include <stdint.h>

#include "lvgl.h"
#include "cJSON.h"
#include "esp_log.h"

#include "line.h"

static const char* TAG = "Line";

static lv_obj_t* lineLabel = nullptr;
static uint32_t lineCount = 0;


void LineHandle(const char* json)
    {
    if (json == nullptr)
        return;

    cJSON* root = cJSON_Parse(json);

    if (root == nullptr)
        {
        ESP_LOGW(TAG, "Invalid LINE JSON");
        return;
        }

    const cJSON* payload =
        cJSON_GetObjectItemCaseSensitive(root, "payload");

    const cJSON* data =
        cJSON_GetObjectItemCaseSensitive(payload, "data");

    if (!cJSON_IsString(data))
        {
        ESP_LOGW(TAG, "LINE payload.data missing or invalid");
        cJSON_Delete(root);
        return;
        }

    cJSON* line = cJSON_Parse(data->valuestring);

    if (line == nullptr)
        {
        ESP_LOGW(TAG, "Invalid LINE data JSON");
        cJSON_Delete(root);
        return;
        }

    const char* message =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(line, "M"));

    const char* alertType =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(line, "T"));

    if (message == nullptr)
        {
        ESP_LOGW(TAG, "LINE message missing");
        cJSON_Delete(line);
        cJSON_Delete(root);
        return;
        }

    char text[512];

    snprintf(
        text,
        sizeof(text),
        "%lu: %s",
        static_cast<unsigned long>(lineCount + 1),
        message
        );

    if (bsp_display_lock(1000) == ESP_OK)
        {
        if (lineLabel == nullptr)
            {
            lineLabel = lv_label_create(lv_screen_active());

            lv_obj_set_width(lineLabel, lv_pct(75));
            lv_label_set_long_mode(lineLabel, LV_LABEL_LONG_WRAP);

            lv_obj_set_style_text_color(
                lineLabel,
                lv_color_white(),
                0
                );

            lv_obj_set_style_text_font(
                lineLabel,
                &lv_font_montserrat_20,
                0
                );

            lv_obj_set_style_text_align(
                lineLabel,
                LV_TEXT_ALIGN_CENTER,
                0
                );

            // Centered horizontally, in the lower part of the display.
            lv_obj_align(lineLabel, LV_ALIGN_BOTTOM_MID, 0, -65);
            }

        lineCount++;

        lv_label_set_text(lineLabel, text);

        bsp_display_unlock();
        }
    else
        {
        ESP_LOGW(TAG, "Failed to acquire LVGL display lock");
        }

    ESP_LOGI(
        TAG,
        "LINE %lu: %s (Type=%s)",
        static_cast<unsigned long>(lineCount),
        message,
        alertType ? alertType : "?"
        );

    cJSON_Delete(line);
    cJSON_Delete(root);
    }

#include "messages.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "esp_log.h"

static const char* TAG = "Messages";

// Dashboard-native 800 x 800 coordinates. Tune only these constants.
static constexpr int MESSAGE_X = 132;
static constexpr int MESSAGE_Y = 398;
static constexpr int MESSAGE_WIDTH = 536;
static constexpr int MESSAGE_ROW_HEIGHT = 38;
static constexpr int MESSAGE_ROW_GAP = 1;
static constexpr int MESSAGE_COUNT = 5;

static constexpr int DATE_X = 9;
static constexpr int DATE_WIDTH = 112;
static constexpr int DOT_X = 125;
static constexpr int DOT_SIZE = 9;
static constexpr int TEXT_X = 146;
static constexpr int TEXT_RIGHT_MARGIN = 10;

static constexpr uint32_t GOLD = 0xD6AC58;
static constexpr uint32_t GREEN = 0x39D875;
static constexpr uint32_t RED = 0xFF4242;
static constexpr uint32_t MUTED = 0xB5A688;

struct LineEntry
{
    std::string text;
    std::string dateTime;
    bool alert = false;
};

struct MessageRow
{
    lv_obj_t* container = nullptr;
    lv_obj_t* dateLabel = nullptr;
    lv_obj_t* dot = nullptr;
    lv_obj_t* textLabel = nullptr;
};

static LineEntry entries[MESSAGE_COUNT];
static MessageRow rows[MESSAGE_COUNT];
static int entryCount = 0;
static bool initialized = false;

// LINE currently carries M=text and T=type; it does not carry a time.
// Use the P4's local clock when it has been set. Before that, show dashes.
static std::string CurrentDateTime()
{
    const time_t now = time(nullptr);
    if (now < 1704067200) // 2024-01-01: clock not initialized
    {
        return "--/-- --:--";
    }

    struct tm local = {};
    if (localtime_r(&now, &local) == nullptr)
    {
        return "--/-- --:--";
    }

    char buffer[24] = {};
    snprintf(buffer, sizeof(buffer), "%02d/%02d %02d:%02d",
             local.tm_mon + 1, local.tm_mday,
             local.tm_hour, local.tm_min);
    return buffer;
}

static bool IsAlert(const char* type)
{
    if (type == nullptr)
    {
        return false;
    }

    // WineSentry alertTYPES includes PROCEDURE, ALERT and CRASH.
    return strcmp(type, "ALERT") == 0 ||
           strcmp(type, "CRASH") == 0 ||
           strcmp(type, "WARNING") == 0;
}

// All LVGL manipulation in this function requires the display lock.
static void RenderMessages()
{
    if (!initialized)
    {
        return;
    }

    // Entries live at the bottom. An incompletely filled list has empty
    // slots at the top, not at the bottom.
    const int firstOccupiedSlot = MESSAGE_COUNT - entryCount;

    for (int slot = 0; slot < MESSAGE_COUNT; ++slot)
    {
        MessageRow& row = rows[slot];
        const bool occupied = slot >= firstOccupiedSlot;
        lv_obj_set_hidden(row.container, !occupied);
        if (!occupied)
        {
            continue;
        }

        const LineEntry& entry = entries[slot - firstOccupiedSlot];
        const bool latest = slot == MESSAGE_COUNT - 1;

        lv_label_set_text(row.dateLabel, entry.dateTime.c_str());
        lv_label_set_text(row.textLabel, entry.text.c_str());
        lv_obj_set_style_bg_color(row.dot,
                                  lv_color_hex(entry.alert ? RED : GREEN), 0);

        lv_obj_set_style_bg_opa(row.container,
                                latest ? LV_OPA_20 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row.container, latest ? 1 : 0, 0);
        lv_obj_set_style_text_color(row.textLabel,
                                    lv_color_hex(latest ? GOLD : MUTED), 0);
        lv_obj_set_style_text_color(row.dateLabel,
                                    lv_color_hex(latest ? GOLD : MUTED), 0);
    }
}

void MessagesInit(lv_obj_t* screen)
{
    // Dashboard initializer already holds bsp_display_lock().
    if (screen == nullptr || initialized)
    {
        return;
    }

    for (int i = 0; i < MESSAGE_COUNT; ++i)
    {
        MessageRow& row = rows[i];
        row.container = lv_obj_create(screen);
        if (row.container == nullptr)
        {
            ESP_LOGE(TAG, "Failed to create message row %d", i);
            return;
        }

        lv_obj_set_pos(row.container, MESSAGE_X,
                       MESSAGE_Y + i * (MESSAGE_ROW_HEIGHT + MESSAGE_ROW_GAP));
        lv_obj_set_size(row.container, MESSAGE_WIDTH, MESSAGE_ROW_HEIGHT);
        lv_obj_set_style_radius(row.container, 6, 0);
        lv_obj_set_style_bg_color(row.container, lv_color_hex(GOLD), 0);
        lv_obj_set_style_bg_opa(row.container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(row.container, lv_color_hex(GOLD), 0);
        lv_obj_set_style_border_width(row.container, 0, 0);
        lv_obj_set_style_pad_all(row.container, 0, 0);
        lv_obj_set_scrollable(row.container, false);
        lv_obj_set_clickable(row.container, false);

        row.dateLabel = lv_label_create(row.container);
        lv_obj_set_pos(row.dateLabel, DATE_X, 10);
        lv_obj_set_width(row.dateLabel, DATE_WIDTH);
        lv_obj_set_style_text_font(row.dateLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(row.dateLabel, lv_color_hex(MUTED), 0);
        lv_label_set_long_mode(row.dateLabel, LV_LABEL_LONG_DOT);

        row.dot = lv_obj_create(row.container);
        lv_obj_set_pos(row.dot, DOT_X, (MESSAGE_ROW_HEIGHT - DOT_SIZE) / 2);
        lv_obj_set_size(row.dot, DOT_SIZE, DOT_SIZE);
        lv_obj_set_style_radius(row.dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(row.dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row.dot, 0, 0);
        lv_obj_set_style_pad_all(row.dot, 0, 0);
        lv_obj_set_scrollable(row.dot, false);

        row.textLabel = lv_label_create(row.container);
        lv_obj_set_pos(row.textLabel, TEXT_X, 9);
        lv_obj_set_width(row.textLabel,
                         MESSAGE_WIDTH - TEXT_X - TEXT_RIGHT_MARGIN);
        lv_obj_set_style_text_font(row.textLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(row.textLabel, lv_color_hex(MUTED), 0);
        lv_label_set_long_mode(row.textLabel, LV_LABEL_LONG_DOT);
        lv_obj_set_height(row.textLabel, 22);
        lv_obj_set_scrollable(row.textLabel, false);

        lv_obj_set_hidden(row.container, true);
    }

    initialized = true;
    RenderMessages();
    ESP_LOGI(TAG, "Five-row LINE message history initialized");
}


// lets get rid of those nasty new line chars
static std::string NormalizeMessageText(const char* text)
{
    std::string result;

    if (text == nullptr)
    {
        return result;
    }

    bool pendingSpace = false;

    for (const char* p = text; *p != '\0'; ++p)
    {
        char c = *p;

        if (c == '\n' || c == '\r' || c == '\t' || c == ' ')
        {
            if (!result.empty())
            {
                pendingSpace = true;
            }

            continue;
        }

        if (pendingSpace)
        {
            result += ' ';
            pendingSpace = false;
        }

        result += c;
    }

    return result;
}

void MessagesHandle(const char* json)
{
    if (json == nullptr)
    {
        return;
    }

    cJSON* root = cJSON_Parse(json);
    if (root == nullptr)
    {
        ESP_LOGW(TAG, "Invalid LINE envelope");
        return;
    }

    // Matches StatusBarHandle's UART envelope:
    // {"payload":{"data":"{\"M\":\"...\",\"T\":\"PROCEDURE\"}"}}
    // Also accept direct M/T objects for easier local testing.
    const cJSON* payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    const cJSON* data = payload != nullptr
        ? cJSON_GetObjectItemCaseSensitive(payload, "data")
        : nullptr;

    cJSON* decoded = nullptr;
    const cJSON* line = root;
    if (cJSON_IsString(data) && data->valuestring != nullptr)
    {
        decoded = cJSON_Parse(data->valuestring);
        if (decoded != nullptr)
        {
            line = decoded;
        }
    }
    else if (cJSON_IsObject(data))
    {
        line = data;
    }

    const char* message = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(line, "M"));
    const char* type = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(line, "T"));

    if (message == nullptr || message[0] == '\0')
    {
        ESP_LOGW(TAG, "LINE missing M text");
        cJSON_Delete(decoded);
        cJSON_Delete(root);
        return;
    }

    LineEntry incoming;
    incoming.text = NormalizeMessageText(message);
    incoming.alert = IsAlert(type);
    // Optional explicit sensor timestamp, for example "11/12 14:05".
    // Current sensor LINE messages do not include DT, so use P4 time.
    const char* suppliedDateTime = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(line, "DT"));
    incoming.dateTime = (suppliedDateTime != nullptr && suppliedDateTime[0] != '\0')
        ? suppliedDateTime
        : CurrentDateTime();

    // Match TEXT exactly, ignoring T; refresh date and type on duplicate.
    if (bsp_display_lock(1000) == ESP_OK)
    {
        if (entryCount > 0 && entries[entryCount - 1].text == incoming.text)
        {
            entries[entryCount - 1] = incoming;
        }
        else
        {
            if (entryCount == MESSAGE_COUNT)
            {
                for (int i = 1; i < MESSAGE_COUNT; ++i)
                {
                    entries[i - 1] = entries[i];
                }
                --entryCount;
            }
            entries[entryCount++] = incoming;
        }

        RenderMessages();
        bsp_display_unlock();
    }
    else
    {
        ESP_LOGW(TAG, "Could not acquire display lock for LINE");
    }

    cJSON_Delete(decoded);
    cJSON_Delete(root);
}

void MessagesClear()
{
    if (bsp_display_lock(1000) != ESP_OK)
    {
        return;
    }

    entryCount = 0;
    for (LineEntry& entry : entries)
    {
        entry = LineEntry{};
    }
    RenderMessages();
    bsp_display_unlock();
}

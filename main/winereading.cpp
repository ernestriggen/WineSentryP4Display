// winereading.cpp

#include <bsp/display.h>
#include <bsp/esp-bsp.h>

#include <cJSON.h>
#include <esp_log.h>
#include <lvgl.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "arc_text.h"
#include "winereading.h"
#include "winereadingicons.h"

static const char* TAG = "WINE_READING";

static lv_obj_t* readingScreen = nullptr;

static lv_obj_t* tempDeviationLabel = nullptr;
static lv_obj_t* humidityDeviationLabel = nullptr;

static lv_obj_t* tempBadge = nullptr;
static lv_obj_t* humidityBadge = nullptr;

static lv_obj_t* tempPercentLabel = nullptr;
static lv_obj_t* humidityPercentLabel = nullptr;

static ArcText readingDateArc;


// Coordinates in the native 800 x 800 dashboard artwork.
static constexpr int TEMP_X = 110;
static constexpr int HUMIDITY_X = 362;

static constexpr int VALUE_Y = 245;
static constexpr int BADGE_Y = 315;

static constexpr int VALUE_WIDTH = 215;
static constexpr int BADGE_WIDTH = 170;

struct WineReadingData
{
    double actualTemp = 0.0;
    double actualHumidity = 0.0;
    double targetTemp = 0.0;
    double targetHumidity = 0.0;

    bool hasTemp = false;
    bool hasHumidity = false;
    bool hasTargetTemp = false;
    bool hasTargetHumidity = false;

    // -1 = unknown, 0 = normal, 1 = out of range.
    int tempOutOfRange = -1;
    int humidityOutOfRange = -1;

    int readingMonth = 0;
    int readingDay = 0;
    int readingHour = 0;
    int readingMinute = 0;
    bool hasReadingDateTime = false;

    int batteryPercent = -1;
    char sensorModel[32] = {};
    char readingStatus[48] = {};
};

static WineReadingData latestReading;
static bool latestReadingAvailable = false;


///////////////////////////////////////////////////////////////////////////////////////////
// Extract a numeric JSON field, accepting either a number or numeric string.
//
static bool GetNumber(
    const cJSON* root,
    const char* name,
    double& result)
{
    const cJSON* item =
        cJSON_GetObjectItemCaseSensitive(root, name);

    if (cJSON_IsNumber(item))
    {
        result = item->valuedouble;
        return std::isfinite(result);
    }

    if (cJSON_IsString(item) && item->valuestring != nullptr)
    {
        char* end = nullptr;
        double value = strtod(item->valuestring, &end);

        if (end == item->valuestring)
            return false;

        while (*end == ' ' || *end == '\t')
            ++end;

        if (*end != '\0' || !std::isfinite(value))
            return false;

        result = value;
        return true;
    }

    return false;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Extract a bounded integer field from the reading JSON.
//
static bool GetInteger(
    const cJSON* root,
    const char* name,
    int minimum,
    int maximum,
    int& result)
{
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);

    if (!cJSON_IsNumber(item))
        return false;

    double value = item->valuedouble;

    if (!std::isfinite(value) ||
        value < minimum ||
        value > maximum ||
        std::floor(value) != value)
        return false;

    result = static_cast<int>(value);
    return true;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Extract a Boolean flag. Unknown is represented by -1.
//
static int GetFlag(const cJSON* root, const char* name)
{
    const cJSON* item =
        cJSON_GetObjectItemCaseSensitive(root, name);

    if (cJSON_IsTrue(item))
        return 1;

    if (cJSON_IsFalse(item))
        return 0;

    if (cJSON_IsNumber(item) &&
        (item->valueint == 0 || item->valueint == 1))
        return item->valueint;

    return -1;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Copy an optional JSON string into owned storage.
//
static void GetText(
    const cJSON* root,
    const char* name,
    char* destination,
    size_t capacity)
{
    const cJSON* item =
        cJSON_GetObjectItemCaseSensitive(root, name);

    if (capacity == 0)
        return;

    destination[0] = '\0';

    if (cJSON_IsString(item) && item->valuestring != nullptr)
    {
        snprintf(destination, capacity, "%s", item->valuestring);
    }
}


///////////////////////////////////////////////////////////////////////////////////////////
// Create a number label on the persistent dashboard.
//
static lv_obj_t* CreateValueLabel(
    lv_obj_t* screen,
    int x,
    int y)
{
    lv_obj_t* label = lv_label_create(screen);

    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, VALUE_WIDTH, LV_SIZE_CONTENT);

    lv_obj_set_style_text_align(
        label,
        LV_TEXT_ALIGN_CENTER,
        0
    );

    lv_obj_set_style_text_font(
        label,
        &lv_font_montserrat_48,
        0
    );

    lv_obj_set_style_text_color(
        label,
        lv_color_hex(0xF3D98D),
        0
    );

    lv_label_set_text(label, "--");

    return label;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Create a rounded percentage indicator beneath a deviation.
//
static void CreateBadge(
    lv_obj_t* screen,
    int x,
    lv_obj_t** badge,
    lv_obj_t** label)
{
    *badge = lv_obj_create(screen);

    lv_obj_set_pos(*badge, x, BADGE_Y);
    lv_obj_set_size(*badge, BADGE_WIDTH, 40);

    lv_obj_set_style_radius(*badge, LV_RADIUS_CIRCLE, 0);

    lv_obj_set_style_bg_color(
        *badge,
        lv_color_hex(0x071408),
        0
    );

    lv_obj_set_style_bg_opa(*badge, LV_OPA_COVER, 0);

    lv_obj_set_style_border_width(*badge, 2, 0);

    lv_obj_set_style_border_color(
        *badge,
        lv_color_hex(0x65FF42),
        0
    );

    lv_obj_set_style_pad_all(*badge, 0, 0);

    lv_obj_remove_flag(*badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(*badge, LV_OBJ_FLAG_CLICKABLE);

    *label = lv_label_create(*badge);

    lv_obj_set_style_text_font(
        *label,
        &lv_font_montserrat_20,
        0
    );

    lv_obj_set_style_text_color(
        *label,
        lv_color_hex(0x65FF42),
        0
    );

    lv_label_set_text(*label, "--");
    lv_obj_center(*label);
}


///////////////////////////////////////////////////////////////////////////////////////////
// Update one instrument and its percentage indicator.
//

static void UpdateInstrument(
    lv_obj_t* valueLabel,
    lv_obj_t* badge,
    lv_obj_t* percentLabel,
    bool hasActual,
    bool hasTarget,
    double actual,
    double target,
    int outOfRange,
    bool temperature)
{
    // Persistent arrow objects for each instrument.
    static lv_obj_t* tempValueArrow = nullptr;
    static lv_obj_t* tempBadgeArrow = nullptr;
    static lv_obj_t* humidityValueArrow = nullptr;
    static lv_obj_t* humidityBadgeArrow = nullptr;

    // LVGL retains pointers to line points, so these must
    // remain valid for the lifetime of the arrow objects.

    static const lv_point_precise_t largeUp[] =
    {
        {0, 9}, {8, 1}, {16, 9}, {8, 1}, {8, 29}
    };

    static const lv_point_precise_t largeDown[] =
    {
        {0, 21}, {8, 29}, {16, 21}, {8, 29}, {8, 1}
    };

    static const lv_point_precise_t smallUp[] =
    {
        {0, 6}, {5, 1}, {10, 6}, {5, 1}, {5, 19}
    };

    static const lv_point_precise_t smallDown[] =
    {
        {0, 14}, {5, 19}, {10, 14}, {5, 19}, {5, 1}
    };

    lv_obj_t** valueArrow =
        temperature ? &tempValueArrow : &humidityValueArrow;

    lv_obj_t** badgeArrow =
        temperature ? &tempBadgeArrow : &humidityBadgeArrow;

    // Create the arrows once.
    if (*valueArrow == nullptr)
    {
        *valueArrow = lv_line_create(valueLabel);

        lv_obj_set_pos(*valueArrow, 3, 12);     // arrow location in relation to the corresponding label

        lv_obj_set_style_line_width(*valueArrow, 4, 0);
        lv_obj_set_style_line_rounded(*valueArrow, true, 0);

        // Reserve room for the arrow inside the label.
        lv_obj_set_style_pad_left(valueLabel, 34, 0);
    }

    if (*badgeArrow == nullptr)
    {
        *badgeArrow = lv_line_create(badge);

        lv_obj_set_pos(*badgeArrow, 42, 9);  // arrow location in relation to the corresponding badge

        lv_obj_set_style_line_width(*badgeArrow, 3, 0);
        lv_obj_set_style_line_rounded(*badgeArrow, true, 0);

        // Reserve 30 pixels on the left for the arrow.
        // Center the percentage within the remaining space.
        lv_obj_set_pos(percentLabel, 30, 5);        // these X,Y are within the badge

        lv_obj_set_size(
                percentLabel,
                BADGE_WIDTH - 30,
                40
        );

        lv_obj_set_style_text_align(
                percentLabel,
                LV_TEXT_ALIGN_CENTER,
                0
        );

        lv_obj_set_style_align(
                percentLabel,
                LV_ALIGN_TOP_LEFT,
                0
        );

        lv_obj_set_style_text_align(
            percentLabel,
            LV_TEXT_ALIGN_CENTER,
            0
        );
    }

    if (!hasActual || !hasTarget)
    {
        lv_label_set_text(valueLabel, "--");
        lv_label_set_text(percentLabel, "--");

        lv_obj_add_flag(*valueArrow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(*badgeArrow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);

        return;
    }

    double deviation = actual - target;

    bool hasPercentage = std::fabs(target) > 0.000001;

    double percentage = hasPercentage
        ? (deviation / target) * 100.0
        : 0.0;

    // Use the precision displayed by the main reading
    // to decide whether to show a direction.
    bool pointingUp = deviation >= 0.005;
    bool pointingDown = deviation <= -0.005;
    bool showArrow = pointingUp || pointingDown;

    lv_color_t valueColor =
        outOfRange == 1
            ? lv_color_hex(0xFF3030)
            : lv_color_hex(0xF3D98D);

    lv_color_t badgeColor;

    if (outOfRange == 1)
        badgeColor = lv_color_hex(0xFF3030);
    else if (outOfRange == 0)
        badgeColor = lv_color_hex(0x65FF42);
    else
        badgeColor = lv_color_hex(0xD6AC58);

    // ---------------------------------------------------------
    // Large deviation reading.
    // ---------------------------------------------------------

    char valueText[48];

    if (temperature)
    {
        snprintf(
            valueText,
            sizeof(valueText),
            "%.2f°",
            std::fabs(deviation)
        );
    }
    else
    {
        snprintf(
            valueText,
            sizeof(valueText),
            "%.2f%%",
            std::fabs(deviation)
        );
    }

    lv_label_set_text(valueLabel, valueText);

    lv_obj_set_style_text_color(
        valueLabel,
        valueColor,
        0
    );

    // ---------------------------------------------------------
    // Large directional arrow.
    // ---------------------------------------------------------

    if (showArrow)
    {
        lv_line_set_points(
            *valueArrow,
            pointingUp ? largeUp : largeDown,
            5
        );

        lv_obj_set_style_line_color(
            *valueArrow,
            valueColor,
            0
        );

        lv_obj_remove_flag(*valueArrow, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(*valueArrow, LV_OBJ_FLAG_HIDDEN);
    }

    // ---------------------------------------------------------
    // Percentage badge.
    // ---------------------------------------------------------

    if (!hasPercentage)
    {
        lv_obj_add_flag(*badgeArrow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    char percentText[48];

    snprintf(
        percentText,
        sizeof(percentText),
        "%.2f%%",
        std::fabs(percentage)
    );

    lv_label_set_text(percentLabel, percentText);

    lv_obj_set_style_text_color(
        percentLabel,
        badgeColor,
        0
    );

    lv_obj_set_style_border_color(
        badge,
        badgeColor,
        0
    );

    lv_obj_set_style_bg_color(
        badge,
        outOfRange == 1
            ? lv_color_hex(0x200505)
            : lv_color_hex(0x071408),
        0
    );

    // ---------------------------------------------------------
    // Small directional arrow inside the badge.
    // ---------------------------------------------------------

    if (showArrow)
    {
        lv_line_set_points(
            *badgeArrow,
            pointingUp ? smallUp : smallDown,
            5
        );

        lv_obj_set_style_line_color(
            *badgeArrow,
            badgeColor,
            0
        );

        lv_obj_remove_flag(*badgeArrow, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(*badgeArrow, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_remove_flag(badge, LV_OBJ_FLAG_HIDDEN);
}


///////////////////////////////////////////////////////////////////////////////////////////
// Render the most recent reading. Caller holds the display lock.
//
static void RenderLatestReading()
{
    if (!latestReadingAvailable || readingScreen == nullptr)
        return;

    UpdateInstrument(
        tempDeviationLabel,
        tempBadge,
        tempPercentLabel,
        latestReading.hasTemp,
        latestReading.hasTargetTemp,
        latestReading.actualTemp,
        latestReading.targetTemp,
        latestReading.tempOutOfRange,
        true
    );

    UpdateInstrument(
        humidityDeviationLabel,
        humidityBadge,
        humidityPercentLabel,
        latestReading.hasHumidity,
        latestReading.hasTargetHumidity,
        latestReading.actualHumidity,
        latestReading.targetHumidity,
        latestReading.humidityOutOfRange,
        false
    );

    // Sensor-provided timestamp. No timezone conversion is performed here.
    if (latestReading.hasReadingDateTime)
    {
        char dateTime[24];
        snprintf(
            dateTime,
            sizeof(dateTime),
            "%02d/%02d %02d:%02d",
            latestReading.readingMonth,
            latestReading.readingDay,
            latestReading.readingHour,
            latestReading.readingMinute);
        ArcTextSetText(readingDateArc, dateTime);
    }
    else
    {
        ArcTextSetText(readingDateArc, "");
    }
}


///////////////////////////////////////////////////////////////////////////////////////////
// Attach our two dynamic instruments to the persistent dashboard.
// Caller holds the BSP display lock.
//
void WineReadingAttachScreen(lv_obj_t* screen)
{
ESP_LOGI(TAG, "WineReadingAttachScreen called: screen=%p",static_cast<void*>(screen));    



    if (screen == nullptr || readingScreen != nullptr)
        return;

    readingScreen = screen;

    tempDeviationLabel =
        CreateValueLabel(screen, TEMP_X, VALUE_Y);

    humidityDeviationLabel =
        CreateValueLabel(screen, HUMIDITY_X, VALUE_Y);

    CreateBadge(
        screen,
        TEMP_X - 3,
        &tempBadge,
        &tempPercentLabel
    );

    CreateBadge(
        screen,
        HUMIDITY_X - 3,
        &humidityBadge,
        &humidityPercentLabel
    );

    // Timestamp follows the outer left edge of the circular instrument.
    // Adjust radius/angles here to fine-tune its position.
    ArcTextInit(
        readingDateArc,
        screen,
        400, 400, 350,
        208.0f, 187.0f,         // these are the beginning and ending 'degrees' of the text
        &lv_font_montserrat_20,
        0xD6AC58);

    if (!WineReadingIconsAttach(screen))
        {
        ESP_LOGW(TAG, "Wine reading icons unavailable");
        }

    RenderLatestReading();

    ESP_LOGI(TAG, "Wine reading instruments created");
}


///////////////////////////////////////////////////////////////////////////////////////////
// Receive a complete WINE_READING envelope from the S3.
//
void WineReadingHandle(const char* json)
{
ESP_LOGI(TAG, "WineReadingHandle ENTERED");

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

    cJSON* ownedReading = nullptr;
    const cJSON* reading = nullptr;

    if (cJSON_IsString(data) && data->valuestring != nullptr)
    {
        ownedReading = cJSON_Parse(data->valuestring);
        reading = ownedReading;
    }
    else if (cJSON_IsObject(data))
    {
        reading = data;
    }

if (cJSON_IsString(data) && data->valuestring != nullptr)
{
    ESP_LOGI(TAG, "WINE_READING JSON: %s", data->valuestring);
}
else if (cJSON_IsObject(data))
{
    ESP_LOGI(TAG, "WINE_READING data supplied as JSON object");
}
else
{
    ESP_LOGW(TAG, "WINE_READING data missing or invalid");
}

    if (!cJSON_IsObject(reading))
    {
        ESP_LOGW(TAG, "Invalid WINE_READING payload.data");
        cJSON_Delete(ownedReading);
        cJSON_Delete(envelope);
        return;
    }

    WineReadingData parsed;

    parsed.hasTemp =
        GetNumber(reading, "ActualTemp", parsed.actualTemp);

    parsed.hasHumidity =
        GetNumber(reading, "ActualHumidity", parsed.actualHumidity);

    parsed.hasTargetTemp =
        GetNumber(reading, "TargetTemp", parsed.targetTemp);

    parsed.hasTargetHumidity =
        GetNumber(reading, "TargetHumidity", parsed.targetHumidity);

    parsed.tempOutOfRange =
        GetFlag(reading, "TempOutOfRange");

    parsed.humidityOutOfRange =
        GetFlag(reading, "HumidityOutOfRange");

    double battery = 0.0;

    if (GetNumber(reading, "BatteryPercent", battery))
        parsed.batteryPercent = static_cast<int>(battery);

    GetText(
        reading,
        "SensorModel",
        parsed.sensorModel,
        sizeof(parsed.sensorModel)
    );

    GetText(
        reading,
        "ReadingStatus",
        parsed.readingStatus,
        sizeof(parsed.readingStatus)
    );

    parsed.hasReadingDateTime =
        GetInteger(reading, "ReadingMonth", 1, 12, parsed.readingMonth) &&
        GetInteger(reading, "ReadingDay", 1, 31, parsed.readingDay) &&
        GetInteger(reading, "ReadingHour", 0, 23, parsed.readingHour) &&
        GetInteger(reading, "ReadingMinute", 0, 59, parsed.readingMinute);

    // Overall normal status is sufficient to mark both normal
    // when individual flags are unavailable.
if (strcmp(parsed.readingStatus, "normal") == 0 ||
    strcmp(parsed.readingStatus, "Normal") == 0 ||
    strcmp(parsed.readingStatus, "NORMAL") == 0)
{
    if (parsed.tempOutOfRange == -1)
        parsed.tempOutOfRange = 0;

    if (parsed.humidityOutOfRange == -1)
        parsed.humidityOutOfRange = 0;
}

    cJSON_Delete(ownedReading);
    cJSON_Delete(envelope);

    if (!parsed.hasTemp && !parsed.hasHumidity)
    {
        ESP_LOGW(TAG, "No valid measurement fields");
        return;
    }

ESP_LOGI(
    TAG,
    "ActualTemp=%.2f TargetTemp=%.2f ActualHumidity=%.2f TargetHumidity=%.2f",
    parsed.actualTemp,
    parsed.targetTemp,
    parsed.actualHumidity,
    parsed.targetHumidity
);

    if (bsp_display_lock(1000) == ESP_OK)
    {
        latestReading = parsed;
        latestReadingAvailable = true;
ESP_LOGI(TAG, "About to render wine reading");
        RenderLatestReading();
ESP_LOGI(TAG, "Wine reading rendered successfully");
        bsp_display_unlock();
    }
    else
    {
        ESP_LOGW(TAG, "Display lock unavailable; reading not applied");
    }

    ESP_LOGI(
        TAG,
        "Wine reading: temp=%.2f target=%.2f, RH=%.2f target=%.2f",
        parsed.actualTemp,
        parsed.targetTemp,
        parsed.actualHumidity,
        parsed.targetHumidity
    );
}

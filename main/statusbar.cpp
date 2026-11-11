
//
// statusbar.cpp
//
// WineSentry dashboard status information.
//
// Displays:
//   Sensor user-defined name at 12 o'clock.
//   Sensor firmware version on the upper-left arc.
//   Sensor health indicator on the left.
//
// Additional status attributes will be added as needed.
//

#include <bsp/esp-bsp.h>
#include <bsp/display.h>

#include <stdio.h>
#include <string.h>

#include "arc_text.h"
#include "cJSON.h"
#include "esp_log.h"
#include "lvgl.h"
#include "models/statusmodels.h"
#include "statusbar.h"

static const char* TAG = "StatusBar";


// /////////////////////////////////////////////////////////////////////////////
// Dashboard geometry — native 800 x 800 coordinates.
// /////////////////////////////////////////////////////////////////////////////

// Sensor name at 12 o'clock.
// These are the existing, working coordinates.

static constexpr int NAME_CENTER_X = 400;
static constexpr int NAME_CENTER_Y = 400;
static constexpr int NAME_RADIUS = 352;

static constexpr float NAME_START_DEGREES = -80.0f; //-78.0f;
static constexpr float NAME_END_DEGREES = -100.0f; // -98.0f;

static constexpr size_t MAX_SENSOR_NAME_LENGTH = 11;
static constexpr size_t SENSOR_NAME_SLOTS = 11;


// Sensor firmware version at approximately 11 o'clock.

static constexpr int SENSOR_VERSION_CENTER_X = 400;
static constexpr int SENSOR_VERSION_CENTER_Y = 400;
static constexpr int SENSOR_VERSION_RADIUS = 350;

static constexpr float SENSOR_VERSION_START_DEGREES = 252.0f;
static constexpr float SENSOR_VERSION_END_DEGREES = 224.0f;


// Left sensor status indicator.

static constexpr int SENSOR_DOT_X = 129;
static constexpr int SENSOR_DOT_Y = 163;
static constexpr int SENSOR_DOT_SIZE = 16;

// /////////////////////////////////////////////////////////////////////////////
// P4 firmware version on the upper-right arc.
// /////////////////////////////////////////////////////////////////////////////

static constexpr int P4_VERSION_CENTER_X = 392; //400;
static constexpr int P4_VERSION_CENTER_Y = 394; //400;
static constexpr int P4_VERSION_RADIUS = 343;

static constexpr float P4_VERSION_START_DEGREES = 316.0f;
static constexpr float P4_VERSION_END_DEGREES = 288.0f;


// Right P4 status indicator.

static constexpr int P4_DOT_X = 654;
static constexpr int P4_DOT_Y = 165;
static constexpr int P4_DOT_SIZE = 16;

static constexpr uint32_t SENSOR_COLOR_NORMAL = 0x39FF14;
static constexpr uint32_t SENSOR_COLOR_DISTRESS = 0xFF3030;

static ArcText sensorIdArc;

static constexpr int SENSOR_ID_RADIUS = 352;

static constexpr float SENSOR_ID_START_DEGREES = 258.0f;
static constexpr float SENSOR_ID_END_DEGREES = 220.0f;


// /////////////////////////////////////////////////////////////////////////////
// LVGL objects.
// /////////////////////////////////////////////////////////////////////////////

static ArcText sensorNameArc;
static ArcText sensorVersionArc;

static lv_obj_t* sensorDot = nullptr;

static bool statusInitialized = false;

static constexpr uint32_t STATUS_SWAP_INTERVAL_MS = 5000;

static lv_timer_t* statusSwapTimer = nullptr;

static bool showPrimaryStatus = true;

static ArcText p4VersionArc;
static lv_obj_t* p4Dot = nullptr;


// /////////////////////////////////////////////////////////////////////////////
// Retained status values.
// /////////////////////////////////////////////////////////////////////////////

static WineSentryIdentity identity;
static WineSentryFirmware firmware;

static bool sensorHealthy = true;


// /////////////////////////////////////////////////////////////////////////////
// Methods called before they are defined.
// /////////////////////////////////////////////////////////////////////////////

static void FormatSensorName(
    const char* source,
    char* destination,
    size_t destinationSize);

static void StatusBarRender();

static void UpdateSensorDot();
static void RenderDisplayFirmwareInformation();

static void StatusSwapTimerCallback(lv_timer_t* timer);

// /////////////////////////////////////////////////////////////////////////////
// Format the user-defined name into a fixed number of character positions.
//
// Long names are truncated.
// Short names are centered by padding with spaces.
//
// ASCII character positions are used for now.
// /////////////////////////////////////////////////////////////////////////////

static void FormatSensorName(
    const char* source,
    char* destination,
    size_t destinationSize)
{
    if (destination == nullptr ||
        destinationSize < SENSOR_NAME_SLOTS + 1)
    {
        return;
    }

    if (source == nullptr)
        source = "";

    char truncated[MAX_SENSOR_NAME_LENGTH + 1] = {};

    snprintf(
        truncated,
        sizeof(truncated),
        "%s",
        source
    );

    size_t length = strlen(truncated);

    size_t leftPadding =
        (SENSOR_NAME_SLOTS - length) / 2;

    memset(
        destination,
        ' ',
        SENSOR_NAME_SLOTS
    );

    memcpy(
        destination + leftPadding,
        truncated,
        length
    );

    destination[SENSOR_NAME_SLOTS] = '\0';
}


// /////////////////////////////////////////////////////////////////////////////
// Update the sensor health indicator.
//
// For now sensorHealthy is initialized to true.
// Distress conditions will be defined later.
//
// Caller must hold the LVGL display lock.
// /////////////////////////////////////////////////////////////////////////////

static void UpdateSensorDot()
{
    if (sensorDot == nullptr)
        return;

    lv_color_t color = lv_color_hex(
        sensorHealthy
            ? SENSOR_COLOR_NORMAL
            : SENSOR_COLOR_DISTRESS
    );

    lv_obj_set_style_bg_color(
        sensorDot,
        color,
        0
    );
}

// /////////////////////////////////////////////////////////////////////////////
// Render the alternating sensor information on the upper-left arc.
//
// Primary:   Sensor v3.0.1
// Secondary: ID ED:D7:7D
//
// Caller must hold the LVGL display lock, or execute within
// the LVGL timer callback.
// /////////////////////////////////////////////////////////////////////////////

static void RenderSensorInformation()
{
    if (!statusInitialized)
        return;

    std::string displayText;

    if (showPrimaryStatus)
    {
        // Display the sensor firmware version.

        if (!firmware.sensorVersion.empty())
        {
            displayText = "Sensor v" + firmware.sensorVersion;
        }
    }
    else
    {
        // Display the final three octets of the LuxSense ID.

        if (!identity.luxSenseID.empty())
        {
                displayText =  identity.luxSenseID;
        }
        else if (!firmware.sensorVersion.empty())
        {
            // Keep showing the version until we receive an ID.

            displayText = "Sensor v" + firmware.sensorVersion;
        }
    }

    ArcTextSetText(
        sensorVersionArc,
        displayText.c_str()
    );
}

// /////////////////////////////////////////////////////////////////////////////
// Render the current dashboard status information.
//
// Caller must hold the LVGL display lock.
// /////////////////////////////////////////////////////////////////////////////

static void StatusBarRender()
{
    if (!statusInitialized)
        return;

    // Sensor name.

    char centeredName[SENSOR_NAME_SLOTS + 1] = {};

    FormatSensorName(
        identity.userDefinedName.c_str(),
        centeredName,
        sizeof(centeredName)
    );

    ArcTextSetText(
        sensorNameArc,
        centeredName
    );

    // Sensor firmware version.

std::string versionText;

if (!firmware.sensorVersion.empty())
{
    versionText = "Sensor v" + firmware.sensorVersion;
}

RenderSensorInformation();
    // Sensor status indicator.

// P4 firmware version.

RenderDisplayFirmwareInformation();
}


// /////////////////////////////////////////////////////////////////////////////
// Initialize dashboard status objects.
//
// Caller must hold the LVGL display lock.
// /////////////////////////////////////////////////////////////////////////////

void StatusBarInit(lv_obj_t* screen)
{
    if (screen == nullptr || statusInitialized)
        return;

    // Sensor user-defined name.

    ArcTextInit(
        sensorNameArc,
        screen,
        NAME_CENTER_X,
        NAME_CENTER_Y,
        NAME_RADIUS,
        NAME_START_DEGREES,
        NAME_END_DEGREES,
        &lv_font_montserrat_20,
        0xD6AC58
    );

    // Sensor firmware version.

    ArcTextInit(
        sensorVersionArc,
        screen,
        SENSOR_VERSION_CENTER_X,
        SENSOR_VERSION_CENTER_Y,
        SENSOR_VERSION_RADIUS,
        SENSOR_VERSION_START_DEGREES,
        SENSOR_VERSION_END_DEGREES,
        &lv_font_montserrat_20,
        0xD6AC58
    );

    ArcTextInit(
    sensorIdArc,
    screen,
    400,
    400,
    SENSOR_ID_RADIUS,
    SENSOR_ID_START_DEGREES,
    SENSOR_ID_END_DEGREES,
    &lv_font_montserrat_14,
    0xD6AC58
)   ;

// P4 firmware version on the upper-right arc.

ArcTextInit(
    p4VersionArc,
    screen,
    P4_VERSION_CENTER_X,
    P4_VERSION_CENTER_Y,
    P4_VERSION_RADIUS,
    P4_VERSION_START_DEGREES,
    P4_VERSION_END_DEGREES,
    &lv_font_montserrat_14,
    0xD6AC58
);

  
    sensorDot = lv_obj_create(screen);

    lv_obj_set_pos(
        sensorDot,
        SENSOR_DOT_X,
        SENSOR_DOT_Y
    );

    lv_obj_set_size(
        sensorDot,
        SENSOR_DOT_SIZE,
        SENSOR_DOT_SIZE
    );

    lv_obj_set_style_radius(
        sensorDot,
        LV_RADIUS_CIRCLE,
        0
    );

    lv_obj_set_style_border_width(
        sensorDot,
        0,
        0
    );

    lv_obj_set_style_pad_all(
        sensorDot,
        0,
        0
    );

    lv_obj_set_style_bg_opa(
        sensorDot,
        LV_OPA_COVER,
        0
    );

lv_obj_set_style_shadow_color(
    sensorDot,
    lv_color_hex(0x39FF14),
    0
);

lv_obj_set_style_shadow_width(
    sensorDot,
    12,
    0
);

lv_obj_set_style_shadow_opa(
    sensorDot,
    LV_OPA_50,
    0
);

lv_obj_set_style_shadow_spread(
    sensorDot,
    2,
    0
);

    lv_obj_set_scrollable(
        sensorDot,
        false
    );

    // /////////////////////////////////////////////////////////////////////////////
// P4 status indicator.
// /////////////////////////////////////////////////////////////////////////////

p4Dot = lv_obj_create(screen);

lv_obj_set_pos(
    p4Dot,
    P4_DOT_X,
    P4_DOT_Y
);

lv_obj_set_size(
    p4Dot,
    P4_DOT_SIZE,
    P4_DOT_SIZE
);

lv_obj_set_style_radius(
    p4Dot,
    LV_RADIUS_CIRCLE,
    0
);

lv_obj_set_style_border_width(
    p4Dot,
    0,
    0
);

lv_obj_set_style_pad_all(
    p4Dot,
    0,
    0
);

lv_obj_set_style_bg_color(
    p4Dot,
    lv_color_hex(0x39FF14),
    0
);

lv_obj_set_style_bg_opa(
    p4Dot,
    LV_OPA_COVER,
    0
);

lv_obj_set_style_shadow_color(
    p4Dot,
    lv_color_hex(0x39FF14),
    0
);

lv_obj_set_style_shadow_width(
    p4Dot,
    12,
    0
);

lv_obj_set_style_shadow_opa(
    p4Dot,
    LV_OPA_50,
    0
);

lv_obj_set_style_shadow_spread(
    p4Dot,
    2,
    0
);

lv_obj_set_scrollable(
    p4Dot,
    false
);
// Initialize the P4 firmware version.

    firmware.p4Version = "v1.0.0";    

    sensorHealthy = true;

    statusInitialized = true;

    UpdateSensorDot();
    StatusBarRender();

    // this is in charge of swapping out statusbar values on the top ring
    statusSwapTimer = lv_timer_create(StatusSwapTimerCallback, STATUS_SWAP_INTERVAL_MS, nullptr);

    ESP_LOGI(
        TAG,
        "Dashboard status initialized"
    );
}


// /////////////////////////////////////////////////////////////////////////////
// Receive and process a STATUSBAR message from the S3.
//
// Outer UART envelope:
// {
//     "payload": {
//         "data": "{\"N\":\"Genesis\",\"V\":\"3.0.1\",...}"
//     }
// }
//
// Currently used:
//   N - Sensor user-defined name.
//   V - Sensor firmware version.
// /////////////////////////////////////////////////////////////////////////////


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

    if (!cJSON_IsString(data) ||
        data->valuestring == nullptr)
    {
        ESP_LOGW(
            TAG,
            "STATUSBAR payload.data missing or invalid"
        );

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

    // Extract sensor identity and firmware information.

    const char* name =
        cJSON_GetStringValue(
            cJSON_GetObjectItemCaseSensitive(status, "N")
        );

    const char* version =
        cJSON_GetStringValue(
            cJSON_GetObjectItemCaseSensitive(status, "V")
        );

    const cJSON* sensitivityField =
        cJSON_GetObjectItemCaseSensitive(status, "S");

    // this will confuse me later --- sometimes S3 sends a fake STATSUBAR message
    // that just has it's S3 firmware version number in it.
    // S3 firmware version.
    // The S3 periodically supplies its own firmware information
    // using a STATUSBAR message containing only "S3V".
    // this will confuse me later --- sometimes S3 sends a fake STATSUBAR message
    // that just has it's S3 firmware version number in it.
    const char* s3Version =
    cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(status, "S3V")
    );


    if (s3Version != nullptr && firmware.s3Version != s3Version)
        {
        firmware.s3Version = s3Version;
    ESP_LOGI(
        TAG,
        "S3 firmware version received: %s",
        firmware.s3Version.c_str()
    );
        }

    bool hasName = name != nullptr;
    bool hasVersion = version != nullptr;
    bool hasSensitivity = cJSON_IsNumber(sensitivityField);

    // Update our status models while holding the display lock.

    if (hasName || hasVersion || hasSensitivity || s3Version != nullptr)
    {
        if (bsp_display_lock(1000) == ESP_OK)
        {
            bool changed = false;

            // Sensor user-defined name.

            if (hasName &&
                identity.userDefinedName != name)
            {
                identity.userDefinedName = name;
                changed = true;
            }

            // Sensor firmware version.

            if (hasVersion &&
                firmware.sensorVersion != version)
            {
                firmware.sensorVersion = version;
                changed = true;
            }

            // Sensor sensitivity.

            if (hasSensitivity)
            {
                identity.sensitivity = sensitivityField->valueint;
                changed = true;
            }

            if (s3Version != nullptr && firmware.s3Version != s3Version)
                {
                firmware.s3Version = s3Version;
                changed = true;
                }            

            // Update the display only when visible information changes.

            if (changed)
            {
                StatusBarRender();

                ESP_LOGI(
                    TAG,
                    "Sensor status updated: name='%s', version='%s', sensitivity=%d",
                    identity.userDefinedName.c_str(),
                    firmware.sensorVersion.c_str(),
                    identity.sensitivity
                );
            }

            bsp_display_unlock();
        }
        else
        {
            ESP_LOGW(
                TAG,
                "Failed to acquire LVGL display lock"
            );
        }
    }

    cJSON_Delete(status);
    cJSON_Delete(root);
}

void setStatusBarLuxSenseID(const char* luxSenseID)
{
    if (luxSenseID == nullptr || luxSenseID[0] == '\0')
        return;

    if (bsp_display_lock(1000) == ESP_OK)
    {
        if (identity.luxSenseID != luxSenseID)
        {
            identity.luxSenseID = luxSenseID;

            ESP_LOGI(
                TAG,
                "LuxSense ID updated: %s",
                identity.luxSenseID.c_str()
            );
        }

        bsp_display_unlock();
    }
    else
    {
        ESP_LOGW(TAG, "Failed to acquire identity model lock");
    }
}

void setStatusBarSensorModel(const char* sensorModel)
{
    if (sensorModel == nullptr || sensorModel[0] == '\0')
        return;

    if (bsp_display_lock(1000) == ESP_OK)
    {
        if (identity.sensorModel != sensorModel)
        {
            identity.sensorModel = sensorModel;

            ESP_LOGI(
                TAG,
                "Sensor model updated: %s",
                identity.sensorModel.c_str()
            );
        }

        bsp_display_unlock();
    }
    else
    {
        ESP_LOGW(TAG, "Failed to acquire identity model lock");
    }
}

// /////////////////////////////////////////////////////////////////////////////
// Render the alternating P4/S3 firmware information.
//
// Primary:   P4 firmware version.
// Secondary: S3 firmware version.
//
// Caller must hold the LVGL display lock or execute within
// the LVGL timer callback.
// /////////////////////////////////////////////////////////////////////////////

static void RenderDisplayFirmwareInformation()
{
    if (!statusInitialized)
        return;

    std::string displayText;

    if (showPrimaryStatus)
    {
        // Display the P4 firmware version.

        if (!firmware.p4Version.empty())
        {
            displayText = "Display " + firmware.p4Version;
        }
    }
    else
    {
        // Display the S3 firmware version.

        if (!firmware.s3Version.empty())
        {
            displayText = "Comms " + firmware.s3Version;
        }
        else if (!firmware.p4Version.empty())
        {
            // Continue displaying P4 until the S3 version arrives.

            displayText = "Display " + firmware.p4Version;
        }
    }

    ArcTextSetText(
        p4VersionArc,
        displayText.c_str()
    );
}

static void StatusSwapTimerCallback(lv_timer_t* timer)
{
    (void)timer;

    if (!statusInitialized)
        return;

    showPrimaryStatus = !showPrimaryStatus;

    // Left side.
    RenderSensorInformation();

    // Right side.
    RenderDisplayFirmwareInformation();
}
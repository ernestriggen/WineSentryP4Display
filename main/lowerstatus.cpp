
// lowerstatus.cpp
//
// WineSentry connectivity indicators.
//
// Two primary 64x64 icons:
//     Wi-Fi strength
//     MQTT connectivity
//
// Small green/red connection dots overlay the lower-right
// corner of each icon.
//
// All six JPEG resources are decoded once and retained.
//
// Normal sensor sleep does not alter the last reported state.
//

#include <cstdlib>
#include <cstdio>
#include <cstring>

#include <driver/jpeg_decode.h>
#include <esp_err.h>
#include <esp_log.h>

#include "lvgl.h"
#include "lowerstatus.h"

static const char* TAG = "LowerStatus";


// /////////////////////////////////////////////////////////////////////////////
// Image dimensions and resources.
// /////////////////////////////////////////////////////////////////////////////

static constexpr int ICON_WIDTH = 64;
static constexpr int ICON_HEIGHT = 64;

static constexpr size_t ICON_BYTES =
    ICON_WIDTH * ICON_HEIGHT * 2;

static constexpr size_t JPEG_CAPACITY = 32 * 1024;


// /////////////////////////////////////////////////////////////////////////////
// Dashboard geometry — native 800 x 800.
//
// Starting coordinates. We will calibrate these visually.
// /////////////////////////////////////////////////////////////////////////////

// Connectivity icons — upper-left dashboard compartment.

static constexpr int WIFI_X = 187;
static constexpr int WIFI_Y = 138;

static constexpr int MQTT_X = 235;
static constexpr int MQTT_Y = 140;

static constexpr int ICON_DISPLAY_SIZE = 40;
static constexpr int DOT_SIZE = 8;

static constexpr int WIFI_DOT_X = WIFI_X + ICON_WIDTH - DOT_SIZE - 21;

static constexpr int WIFI_DOT_Y = WIFI_Y + ICON_HEIGHT - DOT_SIZE - 20;

static constexpr int MQTT_DOT_X = MQTT_X + ICON_WIDTH - DOT_SIZE - 26;

static constexpr int MQTT_DOT_Y = MQTT_Y + ICON_HEIGHT - DOT_SIZE - 24;


// /////////////////////////////////////////////////////////////////////////////
// Colors.
// /////////////////////////////////////////////////////////////////////////////

static constexpr uint32_t DOT_GREEN = 0x39FF14;
static constexpr uint32_t DOT_RED = 0xFF3030;


// /////////////////////////////////////////////////////////////////////////////
// Connection state.
// /////////////////////////////////////////////////////////////////////////////

enum class ConnectionState
{
    UNKNOWN,
    CONNECTED,
    DISCONNECTED
};

static ConnectionState wifiState = ConnectionState::UNKNOWN;
static ConnectionState mqttState = ConnectionState::UNKNOWN;

static bool rssiKnown = false;
static int lastRSSI = -127;

// this is used to darken the icons if we have not heard from the S3 for a while
static constexpr uint32_t STATUSBAR_TIMEOUT_MS = 22UL * 60UL * 1000UL;

static uint32_t lastStatusBarMs = 0;
static bool statusBarReceived = false;

static lv_timer_t* statusBarTimer = nullptr;

// /////////////////////////////////////////////////////////////////////////////
// Persistent JPEG resources.
// /////////////////////////////////////////////////////////////////////////////

struct LowerStatusImage
{
    uint8_t* buffer = nullptr;
    size_t capacity = 0;

    lv_image_dsc_t image = {};
};

static LowerStatusImage wifiUnknownImage;
static LowerStatusImage wifiWeakImage;
static LowerStatusImage wifiGoodImage;
static LowerStatusImage wifiStrongImage;

static LowerStatusImage mqttUnknownImage;
static LowerStatusImage mqttGoldImage;


// /////////////////////////////////////////////////////////////////////////////
// LVGL objects.
// /////////////////////////////////////////////////////////////////////////////

static lv_obj_t* wifiIcon = nullptr;
static lv_obj_t* mqttIcon = nullptr;

static lv_obj_t* wifiDot = nullptr;
static lv_obj_t* mqttDot = nullptr;

static bool lowerStatusInitialized = false;


// /////////////////////////////////////////////////////////////////////////////
// Decode one 64x64 JPEG into a persistent RGB565 buffer.
// /////////////////////////////////////////////////////////////////////////////

static bool LoadImage(
    LowerStatusImage& icon,
    const char* path)
{
    if (icon.buffer != nullptr)
        return true;

    FILE* file = fopen(path, "rb");

    if (file == nullptr)
    {
        ESP_LOGE(TAG, "Cannot open %s", path);
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return false;
    }

    long fileSize = ftell(file);
    rewind(file);

    if (fileSize <= 0 ||
        static_cast<size_t>(fileSize) > JPEG_CAPACITY)
    {
        ESP_LOGE(TAG, "Invalid JPEG size: %s", path);
        fclose(file);
        return false;
    }

    jpeg_decode_memory_alloc_cfg_t inputConfig = {};

    inputConfig.buffer_direction =
        JPEG_DEC_ALLOC_INPUT_BUFFER;

    size_t inputCapacity = 0;

    uint8_t* input = static_cast<uint8_t*>(
        jpeg_alloc_decoder_mem(
            JPEG_CAPACITY,
            &inputConfig,
            &inputCapacity
        )
    );

    if (input == nullptr ||
        inputCapacity < JPEG_CAPACITY)
    {
        ESP_LOGE(TAG, "JPEG input allocation failed");

        fclose(file);
        free(input);

        return false;
    }

    size_t bytesRead = fread(
        input,
        1,
        static_cast<size_t>(fileSize),
        file
    );

    fclose(file);

    if (bytesRead != static_cast<size_t>(fileSize))
    {
        ESP_LOGE(TAG, "JPEG read failed: %s", path);
        free(input);
        return false;
    }

    jpeg_decode_picture_info_t info = {};

    esp_err_t err = jpeg_decoder_get_info(
        input,
        static_cast<uint32_t>(fileSize),
        &info
    );

    if (err != ESP_OK ||
        info.width != ICON_WIDTH ||
        info.height != ICON_HEIGHT)
    {
        ESP_LOGE(
            TAG,
            "Image must be 64x64: %s",
            path
        );

        free(input);
        return false;
    }

    jpeg_decode_memory_alloc_cfg_t outputConfig = {};

    outputConfig.buffer_direction =
        JPEG_DEC_ALLOC_OUTPUT_BUFFER;

    icon.buffer = static_cast<uint8_t*>(
        jpeg_alloc_decoder_mem(
            ICON_BYTES,
            &outputConfig,
            &icon.capacity
        )
    );

    if (icon.buffer == nullptr ||
        icon.capacity < ICON_BYTES)
    {
        ESP_LOGE(TAG, "JPEG output allocation failed");

        free(icon.buffer);
        free(input);

        icon.buffer = nullptr;
        icon.capacity = 0;

        return false;
    }

    jpeg_decode_engine_cfg_t engineConfig = {};

    engineConfig.intr_priority = 0;
    engineConfig.timeout_ms = 1000;

    jpeg_decoder_handle_t decoder = nullptr;

    err = jpeg_new_decoder_engine(
        &engineConfig,
        &decoder
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "JPEG decoder creation failed");

        free(input);
        free(icon.buffer);

        icon.buffer = nullptr;
        icon.capacity = 0;

        return false;
    }

    jpeg_decode_cfg_t decodeConfig = {};

    decodeConfig.output_format =
        JPEG_DECODE_OUT_FORMAT_RGB565;

    // Verified color order from our working P4 project.
    decodeConfig.rgb_order =
        JPEG_DEC_RGB_ELEMENT_ORDER_BGR;

    decodeConfig.conv_std =
        JPEG_YUV_RGB_CONV_STD_BT601;

    uint32_t outputSize = 0;

    err = jpeg_decoder_process(
        decoder,
        &decodeConfig,
        input,
        static_cast<uint32_t>(fileSize),
        icon.buffer,
        static_cast<uint32_t>(icon.capacity),
        &outputSize
    );

    jpeg_del_decoder_engine(decoder);
    free(input);

    if (err != ESP_OK || outputSize < ICON_BYTES)
    {
        ESP_LOGE(TAG, "JPEG decode failed: %s", path);

        free(icon.buffer);

        icon.buffer = nullptr;
        icon.capacity = 0;

        return false;
    }

    memset(&icon.image, 0, sizeof(icon.image));

    icon.image.header.magic = LV_IMAGE_HEADER_MAGIC;
    icon.image.header.cf = LV_COLOR_FORMAT_RGB565;
    icon.image.header.w = ICON_WIDTH;
    icon.image.header.h = ICON_HEIGHT;

    icon.image.data_size = ICON_BYTES;
    icon.image.data = icon.buffer;

    ESP_LOGI(TAG, "Image decoded: %s", path);

    return true;
}


// /////////////////////////////////////////////////////////////////////////////
// Create a tiny connection-status dot.
// /////////////////////////////////////////////////////////////////////////////

static lv_obj_t* CreateStatusDot(
    lv_obj_t* screen,
    int x,
    int y)
{
    lv_obj_t* dot = lv_obj_create(screen);

    if (dot == nullptr)
        return nullptr;

    lv_obj_set_pos(dot, x, y);
    lv_obj_set_size(dot, DOT_SIZE, DOT_SIZE);

    lv_obj_set_style_radius(
        dot,
        LV_RADIUS_CIRCLE,
        0
    );

    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

    lv_obj_set_scrollable(dot, false);
    lv_obj_set_hidden(dot, true);

    return dot;
}


// /////////////////////////////////////////////////////////////////////////////
// Draw the connection-status dot.
// /////////////////////////////////////////////////////////////////////////////

static void UpdateStatusDot(
    lv_obj_t* dot,
    ConnectionState state)
{
    if (dot == nullptr)
        return;

    if (state == ConnectionState::UNKNOWN)
    {
        lv_obj_set_hidden(dot, true);
        return;
    }

    uint32_t color =
        state == ConnectionState::CONNECTED
            ? DOT_GREEN
            : DOT_RED;

    lv_obj_set_style_bg_color(
        dot,
        lv_color_hex(color),
        0
    );

    lv_obj_set_style_shadow_color(
        dot,
        lv_color_hex(color),
        0
    );

    lv_obj_set_style_shadow_width(dot, 4, 0);
    lv_obj_set_style_shadow_opa(dot, LV_OPA_50, 0);

    lv_obj_set_hidden(dot, false);
}


// /////////////////////////////////////////////////////////////////////////////
// Select the Wi-Fi image based on the latest RSSI.
//
// No RSSI yet: faded unknown image.
// /////////////////////////////////////////////////////////////////////////////

static const lv_image_dsc_t* SelectWifiImage()
{
    if (!rssiKnown)
        return &wifiUnknownImage.image;

    if (lastRSSI >= -60)
        return &wifiStrongImage.image;

    if (lastRSSI >= -75)
        return &wifiGoodImage.image;

    return &wifiWeakImage.image;
}


// /////////////////////////////////////////////////////////////////////////////
// Render both connectivity indicators.
//
// Caller must hold the LVGL display lock.
// /////////////////////////////////////////////////////////////////////////////

static void RenderLowerStatus()
{
    if (!lowerStatusInitialized)
        return;

    // Wi-Fi strength.

    lv_image_set_src(
        wifiIcon,
        SelectWifiImage()
    );

    UpdateStatusDot(
        wifiDot,
        wifiState
    );

    // MQTT connection.

    const lv_image_dsc_t* mqttImage =
        mqttState == ConnectionState::UNKNOWN
            ? &mqttUnknownImage.image
            : &mqttGoldImage.image;

    lv_image_set_src(
        mqttIcon,
        mqttImage
    );

    UpdateStatusDot(
        mqttDot,
        mqttState
    );
}


// Called while the LVGL lock is held.

static void ResetConnectivityToUnknown()
{
    wifiState = ConnectionState::UNKNOWN;
    mqttState = ConnectionState::UNKNOWN;

    rssiKnown = false;
    lastRSSI = -127;

    RenderLowerStatus();

    ESP_LOGW(
        TAG,
        "No STATUSBAR for 22 minutes; connectivity indicators reset"
    );
}

// LVGL timer callbacks execute in LVGL context.
// Do not acquire the display lock again here.

static void StatusBarTimeoutCallback(lv_timer_t* timer)
{
    (void)timer;

    if (!lowerStatusInitialized || !statusBarReceived)
    {
        return;
    }

    uint32_t nowMs = lv_tick_get();

    if ((uint32_t)(nowMs - lastStatusBarMs) >= STATUSBAR_TIMEOUT_MS)
    {
        statusBarReceived = false;

        ResetConnectivityToUnknown();
    }
}

// /////////////////////////////////////////////////////////////////////////////
// Initialize both connectivity indicators.
//
// Caller must hold the LVGL display lock.
// /////////////////////////////////////////////////////////////////////////////


bool LowerStatusInit(lv_obj_t* screen)
{
    if (screen == nullptr)
        return false;

    if (lowerStatusInitialized)
        return true;

    // Load the six persistent image resources.

    bool imagesOK = true;

    imagesOK = LoadImage(
        wifiUnknownImage,
        "/spiffs/wifi_unknown_64.jpg"
    ) && imagesOK;

    imagesOK = LoadImage(
        wifiWeakImage,
        "/spiffs/wifi_weak_64.jpg"
    ) && imagesOK;

    imagesOK = LoadImage(
        wifiGoodImage,
        "/spiffs/wifi_good_64.jpg"
    ) && imagesOK;

    imagesOK = LoadImage(
        wifiStrongImage,
        "/spiffs/wifi_strong_64.jpg"
    ) && imagesOK;

    imagesOK = LoadImage(
        mqttUnknownImage,
        "/spiffs/mqttUnknown.jpg"
    ) && imagesOK;

    imagesOK = LoadImage(
        mqttGoldImage,
        "/spiffs/mqtt.jpg"
    ) && imagesOK;

    if (!imagesOK)
    {
        ESP_LOGE(TAG, "Connectivity image loading failed");
        return false;
    }

    // Create Wi-Fi and MQTT image objects FIRST.

    wifiIcon = lv_image_create(screen);
    mqttIcon = lv_image_create(screen);

    if (wifiIcon == nullptr || mqttIcon == nullptr)
    {
        ESP_LOGE(TAG, "LVGL connectivity image creation failed");
        return false;
    }

    // Position the images.

    lv_obj_set_pos(wifiIcon, WIFI_X, WIFI_Y);
    lv_obj_set_pos(mqttIcon, MQTT_X, MQTT_Y);

    // Scale the original 64x64 images to ICON_DISPLAY_SIZE.
    // In LVGL 9, 256 represents 100%.

    static constexpr int IMAGE_SCALE =
        (ICON_DISPLAY_SIZE * 256) / ICON_WIDTH;

    // Anchor the scaling at the upper-left corner.

    lv_image_set_pivot(wifiIcon, 0, 0);
    lv_image_set_pivot(mqttIcon, 0, 0);

    lv_image_set_scale(wifiIcon, IMAGE_SCALE);
    lv_image_set_scale(mqttIcon, IMAGE_SCALE);

    // Create the small lower-right connection dots.

    wifiDot = CreateStatusDot(
        screen,
        WIFI_DOT_X,
        WIFI_DOT_Y
    );

    mqttDot = CreateStatusDot(
        screen,
        MQTT_DOT_X,
        MQTT_DOT_Y
    );

    if (wifiDot == nullptr || mqttDot == nullptr)
    {
        ESP_LOGE(TAG, "Connection-dot creation failed");
        return false;
    }

    lowerStatusInitialized = true;

    RenderLowerStatus();

lastStatusBarMs = lv_tick_get();
statusBarReceived = false;

statusBarTimer = lv_timer_create(
    StatusBarTimeoutCallback,
    60UL * 1000UL,
    nullptr
);    
    ESP_LOGI(
        TAG,
        "Connectivity indicators initialized at %d x %d",
        ICON_DISPLAY_SIZE,
        ICON_DISPLAY_SIZE
    );

    return true;
}

// /////////////////////////////////////////////////////////////////////////////
// Update connectivity from the sensor STATUSBAR.
//
// Missing fields preserve the last reported values.
//
// Caller must hold the LVGL display lock.
// /////////////////////////////////////////////////////////////////////////////

void LowerStatusSetConnectivity(
    const char* wifi,
    const char* mqtt,
    bool hasRSSI,
    int rssi)
{
    bool changed = false;

// Every STATUSBAR refreshes the freshness timeout,
// even if the connectivity values have not changed.

    lastStatusBarMs = lv_tick_get();
    statusBarReceived = true;    

    // Wi-Fi connection state.

    if (wifi != nullptr)
    {
        ConnectionState newState = wifiState;

        if (strcmp(wifi, "Y") == 0)
            newState = ConnectionState::CONNECTED;
        else if (strcmp(wifi, "N") == 0)
            newState = ConnectionState::DISCONNECTED;

        if (newState != wifiState)
        {
            wifiState = newState;
            changed = true;
        }
    }

    // MQTT connection state.

    if (mqtt != nullptr)
    {
        ConnectionState newState = mqttState;

        if (strcmp(mqtt, "Y") == 0)
            newState = ConnectionState::CONNECTED;
        else if (strcmp(mqtt, "N") == 0)
            newState = ConnectionState::DISCONNECTED;

        if (newState != mqttState)
        {
            mqttState = newState;
            changed = true;
        }
    }

    // Retain the most recent usable RSSI measurement.
    // Do not replace it with zero on disconnection.

    if (hasRSSI && rssi < 0 && rssi >= -127)
    {
        if (!rssiKnown || lastRSSI != rssi)
        {
            lastRSSI = rssi;
            rssiKnown = true;
            changed = true;
        }
    }

    if (changed)
    {
        RenderLowerStatus();

        ESP_LOGI(
            TAG,
            "WiFi=%d MQTT=%d RSSI=%d",
            static_cast<int>(wifiState),
            static_cast<int>(mqttState),
            lastRSSI
        );
    }
}

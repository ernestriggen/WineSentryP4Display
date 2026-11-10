// winereadingicons.cpp

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <driver/jpeg_decode.h>
#include <esp_err.h>
#include <esp_log.h>

#include "winereadingicons.h"

static const char* TAG = "WINE_ICONS";

static constexpr int ICON_WIDTH = 48;
static constexpr int ICON_HEIGHT = 64;

static constexpr size_t ICON_BYTES =
    ICON_WIDTH * ICON_HEIGHT * 2;

static constexpr size_t JPEG_CAPACITY = 32 * 1024;

// Icon positions on the 800 x 800 dashboard.
// Icon positions on the 800 x 800 dashboard.
static constexpr int TEMP_ICON_X = 88;
static constexpr int TEMP_ICON_Y = 243;

static constexpr int HUMIDITY_ICON_X = 340;
static constexpr int HUMIDITY_ICON_Y = 241;


///////////////////////////////////////////////////////////////////////////////////////////
// Permanent image resources.
//
struct WineIcon
{
    uint8_t* buffer = nullptr;
    size_t capacity = 0;

    lv_image_dsc_t image = {};
    lv_obj_t* object = nullptr;
};

static WineIcon temperatureIcon;
static WineIcon humidityIcon;

static lv_obj_t* ownerScreen = nullptr;


///////////////////////////////////////////////////////////////////////////////////////////
// Load and decode a JPEG into a persistent RGB565 image buffer.
//
static bool LoadIcon(
    WineIcon& icon,
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
        ESP_LOGE(TAG, "Invalid icon JPEG size: %s", path);
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

    if (input == nullptr || inputCapacity < JPEG_CAPACITY)
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
        ESP_LOGE(TAG, "Icon must be 48x64: %s", path);
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
        ESP_LOGE(TAG, "Icon output allocation failed");

        free(icon.buffer);
        icon.buffer = nullptr;
        icon.capacity = 0;

        free(input);
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

    // Verified color order from our working splash.
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
        ESP_LOGE(TAG, "JPEG decoding failed: %s", path);

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

    ESP_LOGI(TAG, "Icon decoded: %s", path);

    return true;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Create one LVGL icon object.
//
static bool AddIcon(
    lv_obj_t* screen,
    WineIcon& icon,
    const char* path,
    int x,
    int y)
{
    if (icon.object != nullptr)
        return true;

    if (!LoadIcon(icon, path))
        return false;

    icon.object = lv_image_create(screen);

    if (icon.object == nullptr)
    {
        ESP_LOGE(TAG, "LVGL image creation failed: %s", path);
        return false;
    }

    lv_image_set_src(icon.object, &icon.image);

    lv_obj_set_pos(icon.object, x, y);

    lv_obj_remove_flag(
        icon.object,
        LV_OBJ_FLAG_CLICKABLE
    );

    return true;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Attach both gold icons to the dashboard.
// Called with the BSP display lock already held.
//
bool WineReadingIconsAttach(lv_obj_t* screen)
{
    if (screen == nullptr)
        return false;

    // This module is designed for our persistent dashboard.
    if (ownerScreen != nullptr && ownerScreen != screen)
    {
        ESP_LOGE(TAG, "Icons already attached to another screen");
        return false;
    }

    ownerScreen = screen;

    bool tempOK = AddIcon(
        screen,
        temperatureIcon,
        "/spiffs/temperature_icon.jpg",
        TEMP_ICON_X,
        TEMP_ICON_Y
    );

    bool humidityOK = AddIcon(
        screen,
        humidityIcon,
        "/spiffs/humidity_icon.jpg",
        HUMIDITY_ICON_X,
        HUMIDITY_ICON_Y
    );

    if (tempOK && humidityOK)
    {
        ESP_LOGI(TAG, "Both wine reading icons attached");
    }

    return tempOK && humidityOK;
}

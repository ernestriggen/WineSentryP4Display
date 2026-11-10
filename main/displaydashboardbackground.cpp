// displaydashboardbackground.cpp 

#include "displaydashboardbackground.h"

#include <bsp/esp-bsp.h>
#include <bsp/display.h>

#include <driver/jpeg_decode.h>

#include <esp_err.h>
#include <esp_log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* TAG = "DASHBOARD_BG";

static constexpr int WIDTH = 800;
static constexpr int HEIGHT = 800;

static constexpr size_t FRAME_BYTES =
    WIDTH * HEIGHT * 2;

static constexpr size_t JPEG_INPUT_CAPACITY =
    256 * 1024;

static const char* IMAGE_PATH =
    "/spiffs/dashboard.jpg";

// Persistent resources.
static uint8_t* backgroundBuffer = nullptr;
static size_t backgroundCapacity = 0;

static lv_image_dsc_t backgroundImage = {};
static lv_obj_t* backgroundObject = nullptr;


///////////////////////////////////////////////////////////////////////////////////////////
// Load dashboard JPEG and decode it into persistent RGB565 memory.
// The decoder and compressed JPEG buffer are temporary.
//
static bool LoadDashboardImage()
{
    FILE* file = fopen(IMAGE_PATH, "rb");

    if (file == nullptr)
    {
        ESP_LOGE(TAG, "Cannot open %s", IMAGE_PATH);
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
        static_cast<size_t>(fileSize) > JPEG_INPUT_CAPACITY)
    {
        ESP_LOGE(TAG, "Invalid JPEG size: %ld", fileSize);
        fclose(file);
        return false;
    }

    jpeg_decode_memory_alloc_cfg_t inputConfig = {};
    inputConfig.buffer_direction =
        JPEG_DEC_ALLOC_INPUT_BUFFER;

    size_t inputCapacity = 0;

    uint8_t* jpegInput = static_cast<uint8_t*>(
        jpeg_alloc_decoder_mem(
            JPEG_INPUT_CAPACITY,
            &inputConfig,
            &inputCapacity
        )
    );

    if (jpegInput == nullptr ||
        inputCapacity < JPEG_INPUT_CAPACITY)
    {
        ESP_LOGE(TAG, "JPEG input allocation failed");
        fclose(file);
        free(jpegInput);
        return false;
    }

    size_t bytesRead = fread(
        jpegInput,
        1,
        static_cast<size_t>(fileSize),
        file
    );

    fclose(file);

    if (bytesRead != static_cast<size_t>(fileSize))
    {
        ESP_LOGE(TAG, "JPEG read failed");
        free(jpegInput);
        return false;
    }

    jpeg_decode_picture_info_t info = {};

    esp_err_t err = jpeg_decoder_get_info(
        jpegInput,
        static_cast<uint32_t>(fileSize),
        &info
    );

    if (err != ESP_OK ||
        info.width != WIDTH ||
        info.height != HEIGHT)
    {
        ESP_LOGE(TAG, "Dashboard JPEG must be 800x800");
        free(jpegInput);
        return false;
    }

    jpeg_decode_memory_alloc_cfg_t outputConfig = {};
    outputConfig.buffer_direction =
        JPEG_DEC_ALLOC_OUTPUT_BUFFER;

    backgroundBuffer = static_cast<uint8_t*>(
        jpeg_alloc_decoder_mem(
            FRAME_BYTES,
            &outputConfig,
            &backgroundCapacity
        )
    );

    if (backgroundBuffer == nullptr ||
        backgroundCapacity < FRAME_BYTES)
    {
        ESP_LOGE(TAG, "Dashboard RGB565 allocation failed");

        free(backgroundBuffer);
        backgroundBuffer = nullptr;
        backgroundCapacity = 0;

        free(jpegInput);
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
        ESP_LOGE(TAG, "JPEG decoder creation failed: %s",
                 esp_err_to_name(err));

        free(jpegInput);
        free(backgroundBuffer);

        backgroundBuffer = nullptr;
        backgroundCapacity = 0;

        return false;
    }

    jpeg_decode_cfg_t decodeConfig = {};

    decodeConfig.output_format =
        JPEG_DECODE_OUT_FORMAT_RGB565;

    // Same verified color ordering as the splash.
    decodeConfig.rgb_order =
        JPEG_DEC_RGB_ELEMENT_ORDER_BGR;

    decodeConfig.conv_std =
        JPEG_YUV_RGB_CONV_STD_BT601;

    uint32_t outputSize = 0;

    err = jpeg_decoder_process(
        decoder,
        &decodeConfig,
        jpegInput,
        static_cast<uint32_t>(fileSize),
        backgroundBuffer,
        static_cast<uint32_t>(backgroundCapacity),
        &outputSize
    );

    jpeg_del_decoder_engine(decoder);
    free(jpegInput);

    if (err != ESP_OK || outputSize < FRAME_BYTES)
    {
        ESP_LOGE(TAG, "Dashboard JPEG decode failed: %s",
                 esp_err_to_name(err));

        free(backgroundBuffer);

        backgroundBuffer = nullptr;
        backgroundCapacity = 0;

        return false;
    }

    ESP_LOGI(TAG, "Dashboard JPEG decoded successfully");

    memset(&backgroundImage, 0, sizeof(backgroundImage));

    backgroundImage.header.magic = LV_IMAGE_HEADER_MAGIC;
    backgroundImage.header.cf = LV_COLOR_FORMAT_RGB565;
    backgroundImage.header.w = WIDTH;
    backgroundImage.header.h = HEIGHT;

    backgroundImage.data_size = FRAME_BYTES;
    backgroundImage.data = backgroundBuffer;

    return true;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Create the permanent 800x800 LVGL background image.
// This function is called while the display lock is held.
//
bool DisplayDashboardBackgroundInitialize(lv_obj_t* screen)
{
    if (screen == nullptr)
        return false;

    if (backgroundObject != nullptr)
        return lv_obj_get_parent(backgroundObject) == screen;

    if (backgroundBuffer == nullptr)
    {
        if (!LoadDashboardImage())
            return false;
    }

    backgroundObject = lv_image_create(screen);

    if (backgroundObject == nullptr)
    {
        ESP_LOGE(TAG, "Unable to create background object");
        return false;
    }

    lv_image_set_src(backgroundObject, &backgroundImage);
    lv_obj_center(backgroundObject);

    lv_obj_remove_flag(
        backgroundObject,
        LV_OBJ_FLAG_CLICKABLE
    );

    ESP_LOGI(TAG, "Dashboard background created");

    return true;
}

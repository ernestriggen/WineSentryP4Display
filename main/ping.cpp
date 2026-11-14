// ping.cpp

#include "ping.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bsp/esp-bsp.h"
#include "driver/jpeg_decode.h"
#include "esp_log.h"
#include "esp_err.h"

static const char* TAG = "PING";

// Original JPEG dimensions.

static constexpr int ICON_WIDTH = 64;
static constexpr int ICON_HEIGHT = 64;
static constexpr size_t ICON_BYTES =
    ICON_WIDTH * ICON_HEIGHT * 2;

// Visible size on the 800x800 dashboard.
// Adjust this without changing the JPEG files.

static constexpr int ICON_DISPLAY_SIZE = 40;

// Starting position. Adjust to fit the intended compartment.

static constexpr int PING_X = 550;
static constexpr int PING_Y = 140;

static constexpr size_t JPEG_CAPACITY = 32 * 1024;

struct PingImage
{
    uint8_t* buffer = nullptr;
    size_t capacity = 0;
    lv_image_dsc_t image = {};
};

static PingImage heartGoldImage;
static PingImage heartFadedImage;
static PingImage moonImage;

static lv_obj_t* pingIcon = nullptr;

static bool pingInitialized = false;
static bool heartBright = false;
static bool sensorSleeping = true;

static bool LoadPingImage(
    PingImage& destination,
    const char* path
)
{
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

    if (fileSize <= 0 ||
        static_cast<size_t>(fileSize) > JPEG_CAPACITY)
    {
        ESP_LOGE(TAG, "Invalid JPEG file size: %s", path);
        fclose(file);
        return false;
    }

    rewind(file);

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

    if (input == nullptr)
    {
        fclose(file);
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
        free(input);
        return false;
    }

    jpeg_decode_picture_info_t info = {};

    esp_err_t result = jpeg_decoder_get_info(
        input,
        static_cast<uint32_t>(bytesRead),
        &info
    );

    if (result != ESP_OK ||
        info.width != ICON_WIDTH ||
        info.height != ICON_HEIGHT)
    {
        ESP_LOGE(
            TAG,
            "Invalid JPEG dimensions: %s",
            path
        );

        free(input);
        return false;
    }

    jpeg_decode_memory_alloc_cfg_t outputConfig = {};
    outputConfig.buffer_direction =
        JPEG_DEC_ALLOC_OUTPUT_BUFFER;

    size_t outputCapacity = 0;

    uint8_t* output = static_cast<uint8_t*>(
        jpeg_alloc_decoder_mem(
            ICON_BYTES,
            &outputConfig,
            &outputCapacity
        )
    );

    if (output == nullptr)
    {
        free(input);
        return false;
    }

    jpeg_decode_engine_cfg_t engineConfig = {};
    engineConfig.intr_priority = 0;
    engineConfig.timeout_ms = 1000;

    jpeg_decoder_handle_t decoder = nullptr;

    result = jpeg_new_decoder_engine(
        &engineConfig,
        &decoder
    );

    if (result != ESP_OK)
    {
        free(output);
        free(input);
        return false;
    }

    jpeg_decode_cfg_t decodeConfig = {};
    decodeConfig.output_format =
        JPEG_DECODE_OUT_FORMAT_RGB565;
    decodeConfig.rgb_order =
        JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
    decodeConfig.conv_std =
        JPEG_YUV_RGB_CONV_STD_BT601;

    uint32_t outputSize = 0;

    result = jpeg_decoder_process(
        decoder,
        &decodeConfig,
        input,
        static_cast<uint32_t>(bytesRead),
        output,
        static_cast<uint32_t>(outputCapacity),
        &outputSize
    );

    jpeg_del_decoder_engine(decoder);
    free(input);

    if (result != ESP_OK || outputSize < ICON_BYTES)
    {
        ESP_LOGE(TAG, "JPEG decode failed: %s", path);
        free(output);
        return false;
    }

    destination.buffer = output;
    destination.capacity = outputCapacity;

    destination.image.header.magic = LV_IMAGE_HEADER_MAGIC;
    destination.image.header.cf = LV_COLOR_FORMAT_RGB565;
    destination.image.header.w = ICON_WIDTH;
    destination.image.header.h = ICON_HEIGHT;
    destination.image.header.stride = ICON_WIDTH * 2;

    destination.image.data_size = ICON_BYTES;
    destination.image.data = destination.buffer;

    ESP_LOGI(TAG, "Image decoded: %s", path);

    return true;
}

static void RenderPing()
{
    if (!pingInitialized || pingIcon == nullptr)
        return;

    const lv_image_dsc_t* image = nullptr;

    if (sensorSleeping)
    {
        image = &moonImage.image;
    }
    else if (heartBright)
    {
        image = &heartGoldImage.image;
    }
    else
    {
        image = &heartFadedImage.image;
    }

    lv_image_set_src(pingIcon, image);
}

bool PingInit(lv_obj_t* screen)
{
    if (screen == nullptr)
        return false;

    if (pingInitialized)
        return true;

    bool imagesOK = true;

    imagesOK = LoadPingImage(
        heartGoldImage,
        "/spiffs/pingHeartGold.jpg"
    ) && imagesOK;

    imagesOK = LoadPingImage(
        heartFadedImage,
        "/spiffs/pingHeartFaded.jpg"
    ) && imagesOK;

    imagesOK = LoadPingImage(
        moonImage,
        "/spiffs/pingMoon.jpg"
    ) && imagesOK;

    if (!imagesOK)
    {
        ESP_LOGE(TAG, "PING image loading failed");
        return false;
    }

    // Attach explicitly to the dashboard, not
    // whichever LVGL screen happens to be active.

    pingIcon = lv_image_create(screen);

    if (pingIcon == nullptr)
    {
        ESP_LOGE(TAG, "PING image creation failed");
        return false;
    }

    lv_obj_set_pos(
        pingIcon,
        PING_X,
        PING_Y
    );

    // Scale from 64x64 to the chosen display size.

    static constexpr int IMAGE_SCALE =
        (ICON_DISPLAY_SIZE * 256) / ICON_WIDTH;

    lv_image_set_pivot(pingIcon, 0, 0);
    lv_image_set_scale(pingIcon, IMAGE_SCALE);

    pingInitialized = true;

    RenderPing();

    ESP_LOGI(
        TAG,
        "PING indicator initialized at %d x %d",
        ICON_DISPLAY_SIZE,
        ICON_DISPLAY_SIZE
    );

    return true;
}

void PingHandle(bool goingToSleep)
{
    // Record the new state independently of whether
    // the dashboard has finished initializing.

    if (goingToSleep)
    {
        sensorSleeping = true;
    }
    else
    {
        sensorSleeping = false;
        heartBright = !heartBright;
    }

    if (!pingInitialized)
        return;

    if (bsp_display_lock(1000) != ESP_OK)
        return;

    RenderPing();

    bsp_display_unlock();
}

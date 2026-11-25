// winedetailscreen.cpp -- ESP32-P4
// Wine details overlay with a persistent, hardware-decoded JPEG background.
// Uses the exact screen entry points and tap-to-dismiss UART callback from
// the existing implementation. Call WineDetailsScreenShow with the LVGL lock held.

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

#include <driver/jpeg_decode.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_random.h>
#include <lvgl_private.h>
#include <lvgl.h>
#include <time.h>

#include "displaytransmitter.h"
#include "displaywinedetailscreen.h"

LV_FONT_DECLARE(winesentry_serif_24);
LV_FONT_DECLARE(winesentry_sans_16);

static const char* TAG = "WineDetail";

static lv_obj_t* detailsScreen = nullptr;
static int screenSize = 800;

static constexpr uint32_t WHITE = 0xF2E9DB;
static constexpr uint32_t GOLD = 0xD2A75D;
static constexpr uint32_t BACKGROUND = 0x090909;
static constexpr int BACKGROUND_WIDTH = 800;
static constexpr int BACKGROUND_HEIGHT = 800;
static constexpr size_t BACKGROUND_RGB565_BYTES =  BACKGROUND_WIDTH * BACKGROUND_HEIGHT * 2;
static constexpr size_t MAX_BACKGROUND_JPEG_BYTES = 512 * 1024;
static constexpr const char* BACKGROUND_JPEG_PATH = "/spiffs/winedetailbackground.jpg";

// Keep the decoded pixels and LVGL descriptor alive between presentations.
// lv_obj_clean(detailsScreen) deletes children but NOT this image data.
static uint8_t* backgroundPixels = nullptr;
static size_t backgroundCapacity = 0;
static lv_image_dsc_t backgroundDescriptor = {};
static bool backgroundLoaded = false;
static std::atomic<int> g_wineReadingYear{2026};

////////////////////////////////////////////////////////////
// methoids called but defined later
void WineDetailsScreenPrepare(const WinePresentationData& wine);
bool WineDetailsScreenPrepareLabel(const uint8_t* jpeg, size_t jpegSize);
static bool LoadWineLabImage();
static void AddWineLabImage(lv_obj_t* parent);

// New background is laid out at 800 x 800, not the former 720 x 720.
static int S(int value)
{
    return value * screenSize / 800;
}

static lv_obj_t* AddText(
    lv_obj_t* parent,
    int x,
    int y,
    int width,
    const char* text,
    uint32_t color,
    bool centered = false,
    bool large = false)
{
    lv_obj_t* label = lv_label_create(parent);

    lv_obj_set_pos(label, S(x), S(y));
    lv_obj_set_width(label, S(width));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text ? text : "");

    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(
        label,
        centered ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT,
        0
    );

   if (large)
{
    lv_obj_set_style_text_font(
        label,
        &winesentry_serif_24,
        0
    );
}
else
{
    lv_obj_set_style_text_font(
        label,
        &winesentry_sans_16,
        0
    );
}

    // Allow taps to reach the parent overlay.
    lv_obj_set_clickable(label, false);
    return label;
}

// ----------------------------------------------------------
// Dynamic aroma/tasting icons from SPIFFS.
// The same filenames are used for both categories.
// Up to ten visible icons are decoded into RAM per presentation.
// ----------------------------------------------------------
static constexpr int ICON_FILE_SIZE = 192;
static constexpr int ICON_DRAW_SIZE = 55;
static constexpr size_t ICON_PIXELS_BYTES =
    ICON_FILE_SIZE * ICON_FILE_SIZE * 2;
static constexpr size_t ICON_MAX_JPEG_BYTES = 32 * 1024;

struct WineDetailIcon
{
    uint8_t* pixels = nullptr;
    size_t capacity = 0;
    lv_image_dsc_t descriptor = {};
};

static WineDetailIcon iconSlots[10];

static void ClearIconBuffers()
{
    // Only call after lv_obj_clean(detailsScreen), so LVGL no
    // longer references any of these persistent RGB565 buffers.
    for (WineDetailIcon& icon : iconSlots)
    {
        free(icon.pixels);
        icon = {};
    }
}

static bool IsValidIconId(const char* iconId)
{
    if (iconId == nullptr)
    {
        return false;
    }

    const size_t length = strlen(iconId);

    //
    // 27 characters plus ".jpg" = 31 bytes.
    //
    if (length == 0 || length > 27)
    {
        return false;
    }

    for (const char* p = iconId; *p != '\0'; ++p)
    {
        const char c = *p;

        if ((c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '_')
        {
            continue;
        }

        return false;
    }

    return true;
}

static bool LoadIcon(
    WineDetailIcon& destination,
    const char* iconId)
{
    if (!IsValidIconId(iconId))
    {
        ESP_LOGW(
            TAG,
            "Invalid wine icon ID: %s",
            iconId ? iconId : "(null)"
        );

        return false;
    }

    char path[80] = {};

    snprintf(
        path,
        sizeof(path),
        "/spiffs/icons/%s.jpg",
        iconId
    );

    //
    // Existing JPEG loading and decoding code
    // continues unchanged from here.
    //
    FILE* file = fopen(path, "rb");
    if (file == nullptr)
    {
        ESP_LOGW(TAG, "Icon file missing: %s", path);
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return false;
    }

    const long fileSize = ftell(file);
    rewind(file);

    if (fileSize <= 0 || static_cast<size_t>(fileSize) > ICON_MAX_JPEG_BYTES)
    {
        fclose(file);
        ESP_LOGW(TAG, "Icon JPEG has invalid size: %s", path);
        return false;
    }

    jpeg_decode_memory_alloc_cfg_t inputConfig = {};
    inputConfig.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER;
    size_t inputCapacity = 0;
    uint8_t* input = static_cast<uint8_t*>(jpeg_alloc_decoder_mem(
        static_cast<size_t>(fileSize), &inputConfig, &inputCapacity));

    if (input == nullptr || inputCapacity < static_cast<size_t>(fileSize))
    {
        fclose(file);
        free(input);
        ESP_LOGE(TAG, "Icon JPEG input allocation failed: %s", path);
        return false;
    }

    const size_t bytesRead = fread(input, 1, static_cast<size_t>(fileSize), file);
    fclose(file);
    if (bytesRead != static_cast<size_t>(fileSize))
    {
        free(input);
        return false;
    }

    jpeg_decode_picture_info_t info = {};
    esp_err_t err = jpeg_decoder_get_info(
        input, static_cast<uint32_t>(fileSize), &info);

    if (err != ESP_OK || info.width != ICON_FILE_SIZE ||
        info.height != ICON_FILE_SIZE)
    {
        free(input);
        ESP_LOGW(TAG, "Icon JPEG must be 192x192: %s", path);
        return false;
    }

    jpeg_decode_memory_alloc_cfg_t outputConfig = {};
    outputConfig.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
    destination.pixels = static_cast<uint8_t*>(jpeg_alloc_decoder_mem(
        ICON_PIXELS_BYTES, &outputConfig, &destination.capacity));

    if (destination.pixels == nullptr || destination.capacity < ICON_PIXELS_BYTES)
    {
        free(input);
        free(destination.pixels);
        destination = {};
        ESP_LOGE(TAG, "Icon RGB565 allocation failed: %s", path);
        return false;
    }

    jpeg_decode_engine_cfg_t engineConfig = {};
    engineConfig.intr_priority = 0;
    engineConfig.timeout_ms = 1000;
    jpeg_decoder_handle_t decoder = nullptr;
    err = jpeg_new_decoder_engine(&engineConfig, &decoder);

    if (err != ESP_OK)
    {
        free(input);
        free(destination.pixels);
        destination = {};
        return false;
    }

    jpeg_decode_cfg_t decodeConfig = {};
    decodeConfig.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
    decodeConfig.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
    decodeConfig.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;
    uint32_t outputSize = 0;

    err = jpeg_decoder_process(
        decoder, &decodeConfig, input, static_cast<uint32_t>(fileSize),
        destination.pixels, static_cast<uint32_t>(destination.capacity), &outputSize);

    jpeg_del_decoder_engine(decoder);
    free(input);

    if (err != ESP_OK || outputSize < ICON_PIXELS_BYTES)
    {
        ESP_LOGE(TAG, "Icon JPEG decode failed: %s", path);
        free(destination.pixels);
        destination = {};
        return false;
    }

    destination.descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    destination.descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    destination.descriptor.header.w = ICON_FILE_SIZE;
    destination.descriptor.header.h = ICON_FILE_SIZE;
    destination.descriptor.data_size = ICON_PIXELS_BYTES;
    destination.descriptor.data = destination.pixels;
    ESP_LOGI(TAG, "Wine icon decoded: %s", path);
    return true;
}


//
// Display up to five WineSentry descriptors.
//
// The Dart imaging backend supplies:
//   labels[]  - The caption to display.
//   iconIds[] - The SPIFFS icon identifier.
//
// An empty icon ID means display text only.
// No phrase interpretation or normalization happens here.
//

static void AddIconRow(
    lv_obj_t* parent,
    int imageY,
    const std::array<std::string, 5>& labels,
    const std::array<std::string, 5>& iconIds,
    int firstSlot)
{
    static constexpr int COLUMN_CENTERS[5][5] =
    {
        {400,   0,   0,   0,   0},
        {350, 450,   0,   0,   0},
        {300, 400, 500,   0,   0},
        {250, 350, 450, 550,   0},
        {200, 300, 400, 500, 600}
    };

    int indices[5] = {};
    int count = 0;

    //
    // Count descriptors that have display captions.
    // An icon is NOT required.
    //
    for (int i = 0; i < 5; ++i)
    {
        if (!labels[i].empty())
        {
            indices[count++] = i;
        }
    }

    if (count == 0)
    {
        return;
    }

    //
    // Display each caption and its associated icon.
    //
    for (int column = 0; column < count; ++column)
    {
        const int i = indices[column];
        const int centerX = COLUMN_CENTERS[count - 1][column];

        const std::string& caption = labels[i];
        const std::string& iconId = iconIds[i];

        //
        // Display icon only when the backend supplied one.
        //
        if (!iconId.empty())
        {
            WineDetailIcon& icon = iconSlots[firstSlot + i];

            if (LoadIcon(icon, iconId.c_str()))
            {
                lv_obj_t* image = lv_image_create(parent);

                lv_image_set_src(image, &icon.descriptor);
                lv_image_set_pivot(image, 0, 0);

                lv_image_set_scale(
                    image,
                    ICON_DRAW_SIZE * 256 /
                    ICON_FILE_SIZE * screenSize / 800
                );

                lv_obj_set_pos(
                    image,
                    S(centerX - ICON_DRAW_SIZE / 2),
                    S(imageY)
                );

                lv_obj_set_clickable(image, false);
                lv_obj_set_scrollable(image, false);
            }
            else
            {
                ESP_LOGW(
                    TAG,
                    "Icon unavailable: %s for %s",
                    iconId.c_str(),
                    caption.c_str()
                );
            }
        }
        else
        {
            ESP_LOGI(
                TAG,
                "No icon assigned: %s",
                caption.c_str()
            );
        }

        //
        // Always display the backend-provided caption.
        // No interpretation or substitution on the P4.
        //
        AddText(
            parent,
            centerX - 55,
            imageY + ICON_DRAW_SIZE + 2,
            110,
            caption.c_str(),
            WHITE,
            true
        );
    }
}
// ----------------------------------------------------------
// Wine label JPEG: decoded outside the LVGL lock, displayed
// inside the gold frame already baked into the background.
// ----------------------------------------------------------
static constexpr int LABEL_X = 144;
static constexpr int LABEL_Y = 161;
static constexpr int LABEL_WIDTH = 199;
static constexpr int LABEL_HEIGHT = 264;
static constexpr int MAX_LABEL_DIMENSION = 1024;
static constexpr size_t MAX_LABEL_DECODE_BYTES = 2 * 1024 * 1024;

struct DetailLabelImage
{
    uint8_t* pixels = nullptr;
    size_t capacity = 0;
    lv_image_dsc_t descriptor = {};
};

static DetailLabelImage preparedLabel;
static DetailLabelImage displayedLabel;
static lv_obj_t* wineLabelObject = nullptr;

static void ReleaseLabel(DetailLabelImage& image)
{
    free(image.pixels);
    image.pixels = nullptr;
    image.capacity = 0;
    memset(&image.descriptor, 0, sizeof(image.descriptor));
}


///////////////////////////////////////////////////////////////////////////////////////////
// Prepare the wine's display resources outside the LVGL lock.
//
void WineDetailsScreenPrepare(const WinePresentationData& wine)
{
    if (!wine.labelJpeg.empty())
    {
        if (!WineDetailsScreenPrepareLabel(
                wine.labelJpeg.data(),
                wine.labelJpeg.size()))
        {
            ESP_LOGE(TAG, "Unable to prepare wine label");
        }
    }
}

// This function performs NO LVGL operations. 
bool WineDetailsScreenPrepareLabel(const uint8_t* jpeg, size_t jpegSize)
{
    ReleaseLabel(preparedLabel);

    if (jpeg == nullptr || jpegSize < 4 ||
        jpegSize > 64 * 1024 ||
        jpeg[0] != 0xFF || jpeg[1] != 0xD8 ||
        jpeg[jpegSize - 2] != 0xFF || jpeg[jpegSize - 1] != 0xD9)
    {
        ESP_LOGW(TAG, "Invalid wine-label JPEG input");
        return false;
    }

    // The P4 JPEG hardware requires an aligned input buffer.
    jpeg_decode_memory_alloc_cfg_t inputConfig = {};
    inputConfig.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER;
    size_t inputCapacity = 0;
    uint8_t* input = static_cast<uint8_t*>(
        jpeg_alloc_decoder_mem(jpegSize, &inputConfig, &inputCapacity));

    if (input == nullptr || inputCapacity < jpegSize)
    {
        free(input);
        ESP_LOGE(TAG, "Wine label input allocation failed");
        return false;
    }

    memcpy(input, jpeg, jpegSize);

    jpeg_decode_picture_info_t info = {};
    esp_err_t err = jpeg_decoder_get_info(
        input, static_cast<uint32_t>(jpegSize), &info);

    if (err != ESP_OK || info.width == 0 || info.height == 0 ||
        info.width > MAX_LABEL_DIMENSION ||
        info.height > MAX_LABEL_DIMENSION)
    {
        ESP_LOGE(TAG, "Unsupported wine-label JPEG dimensions");
        free(input);
        return false;
    }

    // YUV420 / YUV422 JPEGs can produce padded dimensions.
    // Allocate for both dimensions rounded up to 16 pixels.
    const size_t paddedWidth = (static_cast<size_t>(info.width) + 15) & ~size_t(15);
    const size_t paddedHeight = (static_cast<size_t>(info.height) + 15) & ~size_t(15);
    const size_t paddedBytes = paddedWidth * paddedHeight * 2;

    if (paddedBytes > MAX_LABEL_DECODE_BYTES)
    {
        ESP_LOGE(TAG, "Wine label exceeds decoded image limit");
        free(input);
        return false;
    }

    jpeg_decode_memory_alloc_cfg_t outputConfig = {};
    outputConfig.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
    size_t capacity = 0;
    uint8_t* pixels = static_cast<uint8_t*>(
        jpeg_alloc_decoder_mem(paddedBytes, &outputConfig, &capacity));

    if (pixels == nullptr || capacity < paddedBytes)
    {
        ESP_LOGE(TAG, "Wine label RGB565 allocation failed");
        free(pixels);
        free(input);
        return false;
    }

    jpeg_decode_engine_cfg_t engineConfig = {};
    engineConfig.intr_priority = 0;
    engineConfig.timeout_ms = 1000;
    jpeg_decoder_handle_t decoder = nullptr;
    err = jpeg_new_decoder_engine(&engineConfig, &decoder);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Wine label JPEG engine failed: %s", esp_err_to_name(err));
        free(pixels);
        free(input);
        return false;
    }

    jpeg_decode_cfg_t decodeConfig = {};
    decodeConfig.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
    decodeConfig.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
    decodeConfig.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;

    uint32_t outputSize = 0;
    err = jpeg_decoder_process(
        decoder, &decodeConfig, input, static_cast<uint32_t>(jpegSize),
        pixels, static_cast<uint32_t>(capacity), &outputSize);

    jpeg_del_decoder_engine(decoder);
    free(input);

    const size_t visibleBytes = static_cast<size_t>(info.width) * info.height * 2;
    if (err != ESP_OK || outputSize < visibleBytes)
    {
        ESP_LOGE(TAG, "Wine label JPEG decoding failed: %s", esp_err_to_name(err));
        free(pixels);
        return false;
    }

    // The decoder may produce padded rows. Compact in place so
    // LVGL receives tightly packed pixels with the visible width.
    // Determine stride from the output size rather than assuming it.
    size_t sourceStride = 0;
    if (outputSize == paddedWidth * paddedHeight * 2)
    {
        sourceStride = paddedWidth * 2;
    }
    else if (outputSize == static_cast<size_t>(info.width) * paddedHeight * 2 ||
             outputSize == visibleBytes)
    {
        sourceStride = static_cast<size_t>(info.width) * 2;
    }
    else if (outputSize == paddedWidth * static_cast<size_t>(info.height) * 2)
    {
        sourceStride = paddedWidth * 2;
    }
    else
    {
        ESP_LOGE(TAG, "Unexpected wine label decoder output: %u bytes", outputSize);
        free(pixels);
        return false;
    }

    const size_t visibleStride = static_cast<size_t>(info.width) * 2;
    if (sourceStride != visibleStride)
    {
        for (size_t row = 1; row < info.height; ++row)
        {
            memmove(pixels + row * visibleStride,
                    pixels + row * sourceStride,
                    visibleStride);
        }
    }

    preparedLabel.pixels = pixels;
    preparedLabel.capacity = capacity;
    preparedLabel.descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    preparedLabel.descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    preparedLabel.descriptor.header.w = info.width;
    preparedLabel.descriptor.header.h = info.height;
    preparedLabel.descriptor.data_size = visibleBytes;
    preparedLabel.descriptor.data = pixels;

    ESP_LOGI(TAG, "Wine label decoded: %ux%u, %u bytes",
             static_cast<unsigned>(info.width),
             static_cast<unsigned>(info.height),
             static_cast<unsigned>(outputSize));
    return true;
}

// Call with the LVGL lock held, AFTER WineDetailsScreenShow().
void WineDetailsScreenApplyLabel()
{
    if (detailsScreen == nullptr || preparedLabel.pixels == nullptr)
    {
        return;
    }

    // Drop the previous image object before releasing its pixel buffer.
    if (wineLabelObject != nullptr)
    {
        lv_obj_delete(wineLabelObject);
        wineLabelObject = nullptr;
    }
    ReleaseLabel(displayedLabel);
    displayedLabel = preparedLabel;
    preparedLabel = {};

    lv_obj_t* labelImage = lv_image_create(detailsScreen);
    wineLabelObject = labelImage;
    if (labelImage == nullptr)
    {
        ReleaseLabel(displayedLabel);
        return;
    }

    lv_image_set_src(labelImage, &displayedLabel.descriptor);

    const int w = displayedLabel.descriptor.header.w;
    const int h = displayedLabel.descriptor.header.h;
    const int scaleX = LABEL_WIDTH * 256 / w;
    const int scaleY = LABEL_HEIGHT * 256 / h;
    const int scale = (scaleX < scaleY ? scaleX : scaleY);
    const int renderW = w * scale / 256;
    const int renderH = h * scale / 256;

    lv_image_set_pivot(labelImage, 0, 0);
    lv_image_set_scale(labelImage, scale * screenSize / 800);
    lv_obj_set_pos(labelImage,
                   S(LABEL_X + (LABEL_WIDTH - renderW) / 2),
                   S(LABEL_Y + (LABEL_HEIGHT - renderH) / 2));
    lv_obj_set_clickable(labelImage, false);
    lv_obj_set_scrollable(labelImage, false);
    lv_obj_move_foreground(labelImage);
}

// ----------------------------------------------------------
// Temporary rating artwork test: always display 70w.jpg.
// The JPEG is decoded once and kept alive like the background.
// Final rating selection/layout will replace this test later.
// ----------------------------------------------------------
static constexpr const char* TEST_RATING_JPEG_PATH = "/spiffs/70w.jpg";
static constexpr int TEST_RATING_FILE_WIDTH = 240;
static constexpr int TEST_RATING_FILE_HEIGHT = 136;
static constexpr int TEST_RATING_X = 375;  // left is decrease
static constexpr int TEST_RATING_Y = 315;  // up is descrease
static constexpr int TEST_RATING_DRAW_WIDTH = 240;
static constexpr int TEST_RATING_DRAW_HEIGHT = 136;
static constexpr size_t MAX_TEST_RATING_JPEG_BYTES = 64 * 1024;

static uint8_t* ratingPixels = nullptr;
static lv_image_dsc_t ratingDescriptor = {};
static bool ratingLoaded = false;

static bool LoadRatingImage(double ratingScore)
{
    if (ratingScore < 0.0)
    {
        return false;
    }

    // Previous LVGL image objects have already been deleted
    // by WineDetailsScreenShow().
    free(ratingPixels);
    ratingPixels = nullptr;
    ratingDescriptor = {};

    // Round to the nearest 10.
    int ratingBand = (static_cast<int>(ratingScore + 5.0) / 10) * 10;

    // Available artwork ranges from 60 through 100.
    if (ratingBand < 60) ratingBand = 60;
    if (ratingBand > 100) ratingBand = 100;

    // Randomly choose m or w for each presentation.
    char suffix = (esp_random() & 1U) ? 'w' : 'm';

    char ratingPath[48] = {};

    snprintf(ratingPath, sizeof(ratingPath), "/spiffs/%d%c.jpg", ratingBand, suffix);

    ESP_LOGI(TAG, "Loading rating image: %s", ratingPath);

    FILE* file = fopen(ratingPath, "rb");
    if (file == nullptr)
    {
        ESP_LOGW(TAG, "Cannot open rating JPEG: %s", TEST_RATING_JPEG_PATH);
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return false;
    }

    const long fileSize = ftell(file);
    rewind(file);
    if (fileSize <= 0 ||
        static_cast<size_t>(fileSize) > MAX_TEST_RATING_JPEG_BYTES)
    {
        ESP_LOGW(TAG, "Invalid rating JPEG size: %ld", fileSize);
        fclose(file);
        return false;
    }

    jpeg_decode_memory_alloc_cfg_t inputConfig = {};
    inputConfig.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER;
    size_t inputCapacity = 0;
    uint8_t* input = static_cast<uint8_t*>(jpeg_alloc_decoder_mem(
        static_cast<size_t>(fileSize), &inputConfig, &inputCapacity));
    if (input == nullptr || inputCapacity < static_cast<size_t>(fileSize))
    {
        fclose(file);
        free(input);
        ESP_LOGE(TAG, "Rating JPEG input allocation failed");
        return false;
    }

    const size_t bytesRead = fread(input, 1, static_cast<size_t>(fileSize), file);
    fclose(file);
    if (bytesRead != static_cast<size_t>(fileSize))
    {
        free(input);
        return false;
    }

    jpeg_decode_picture_info_t info = {};
    esp_err_t err = jpeg_decoder_get_info(
        input, static_cast<uint32_t>(fileSize), &info);
    if (err != ESP_OK ||
        info.width != TEST_RATING_FILE_WIDTH ||
        info.height != TEST_RATING_FILE_HEIGHT)
    {
        ESP_LOGE(TAG, "Rating JPEG must be %dx%d: %s",
                 TEST_RATING_FILE_WIDTH, TEST_RATING_FILE_HEIGHT,
                 TEST_RATING_JPEG_PATH);
        free(input);
        return false;
    }

    // The hardware JPEG decoder may pad the output height to 16 pixels.
    const size_t paddedWidth = (static_cast<size_t>(info.width) + 15) & ~size_t(15);
    const size_t paddedHeight = (static_cast<size_t>(info.height) + 15) & ~size_t(15);
    const size_t allocationBytes = paddedWidth * paddedHeight * 2;
    const size_t visibleBytes = static_cast<size_t>(info.width) * info.height * 2;

    jpeg_decode_memory_alloc_cfg_t outputConfig = {};
    outputConfig.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
    size_t outputCapacity = 0;
    uint8_t* pixels = static_cast<uint8_t*>(jpeg_alloc_decoder_mem(
        allocationBytes, &outputConfig, &outputCapacity));
    if (pixels == nullptr || outputCapacity < allocationBytes)
    {
        free(input);
        free(pixels);
        ESP_LOGE(TAG, "Rating JPEG output allocation failed");
        return false;
    }

    jpeg_decode_engine_cfg_t engineConfig = {};
    engineConfig.intr_priority = 0;
    engineConfig.timeout_ms = 1000;
    jpeg_decoder_handle_t decoder = nullptr;
    err = jpeg_new_decoder_engine(&engineConfig, &decoder);
    if (err != ESP_OK)
    {
        free(input);
        free(pixels);
        ESP_LOGE(TAG, "Rating JPEG engine unavailable: %s", esp_err_to_name(err));
        return false;
    }

    jpeg_decode_cfg_t decodeConfig = {};
    decodeConfig.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
    decodeConfig.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
    decodeConfig.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;
    uint32_t outputSize = 0;
    err = jpeg_decoder_process(
        decoder, &decodeConfig, input, static_cast<uint32_t>(fileSize),
        pixels, static_cast<uint32_t>(outputCapacity), &outputSize);

    jpeg_del_decoder_engine(decoder);
    free(input);
    if (err != ESP_OK || outputSize < visibleBytes)
    {
        ESP_LOGE(TAG, "Rating JPEG decode failed: %s", esp_err_to_name(err));
        free(pixels);
        return false;
    }

    // Compact rows if the decoder returned padded RGB565 scanlines.
    size_t sourceStride = 0;
    if (outputSize == paddedWidth * paddedHeight * 2 ||
        outputSize == paddedWidth * static_cast<size_t>(info.height) * 2)
    {
        sourceStride = paddedWidth * 2;
    }
    else if (outputSize == static_cast<size_t>(info.width) * paddedHeight * 2 ||
             outputSize == visibleBytes)
    {
        sourceStride = static_cast<size_t>(info.width) * 2;
    }
    else
    {
        ESP_LOGE(TAG, "Unexpected rating JPEG output size: %u", outputSize);
        free(pixels);
        return false;
    }

    const size_t visibleStride = static_cast<size_t>(info.width) * 2;
    if (sourceStride != visibleStride)
    {
        for (size_t row = 1; row < info.height; ++row)
        {
            memmove(pixels + row * visibleStride,
                    pixels + row * sourceStride, visibleStride);
        }
    }

    ratingPixels = pixels;
    ratingDescriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    ratingDescriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    ratingDescriptor.header.w = info.width;
    ratingDescriptor.header.h = info.height;
    ratingDescriptor.data_size = visibleBytes;
    ratingDescriptor.data = ratingPixels;
    ratingLoaded = true;
    ESP_LOGI(TAG, "Test rating JPEG decoded: %s (%ux%u)",
             TEST_RATING_JPEG_PATH,
             static_cast<unsigned>(info.width),
             static_cast<unsigned>(info.height));
    return true;
}

static void AddRatingImage(lv_obj_t* parent, double ratingScore)
{
    if (!LoadRatingImage(ratingScore))
    {
        return;
    }

    lv_obj_t* image = lv_image_create(parent);
    lv_image_set_src(image, &ratingDescriptor);
    lv_image_set_pivot(image, 0, 0);
    // The supplied art is 240x136; render at 120x68 in the existing rating box.
    lv_image_set_scale(image,
                       TEST_RATING_DRAW_WIDTH * 256 /
                       TEST_RATING_FILE_WIDTH * screenSize / 800);
    lv_obj_set_pos(image, S(TEST_RATING_X), S(TEST_RATING_Y));
    lv_obj_set_clickable(image, false);
    lv_obj_set_scrollable(image, false);
}

static bool LoadBackgroundImage()
{
    if (backgroundLoaded)
    {
        return true;
    }

    FILE* file = fopen(BACKGROUND_JPEG_PATH, "rb");
    if (file == nullptr)
    {
        ESP_LOGE(TAG, "Cannot open background: %s", BACKGROUND_JPEG_PATH);
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
        static_cast<size_t>(fileSize) > MAX_BACKGROUND_JPEG_BYTES)
    {
        ESP_LOGE(TAG, "Invalid background JPEG size: %ld", fileSize);
        fclose(file);
        return false;
    }

    jpeg_decode_memory_alloc_cfg_t inputConfig = {};
    inputConfig.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER;

    size_t inputCapacity = 0;
    uint8_t* input = static_cast<uint8_t*>(
        jpeg_alloc_decoder_mem(
            static_cast<size_t>(fileSize),
            &inputConfig,
            &inputCapacity
        )
    );

    if (input == nullptr ||
        inputCapacity < static_cast<size_t>(fileSize))
    {
        ESP_LOGE(TAG, "Cannot allocate background JPEG input");
        fclose(file);
        free(input);
        return false;
    }

    size_t bytesRead = fread(input, 1, static_cast<size_t>(fileSize), file);
    fclose(file);

    if (bytesRead != static_cast<size_t>(fileSize))
    {
        ESP_LOGE(TAG, "Background JPEG read incomplete");
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
        info.width != BACKGROUND_WIDTH ||
        info.height != BACKGROUND_HEIGHT)
    {
        ESP_LOGE(TAG, "Expected %dx%d background JPEG",
                 BACKGROUND_WIDTH, BACKGROUND_HEIGHT);
        free(input);
        return false;
    }

    jpeg_decode_memory_alloc_cfg_t outputConfig = {};
    outputConfig.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;

    backgroundPixels = static_cast<uint8_t*>(
        jpeg_alloc_decoder_mem(
            BACKGROUND_RGB565_BYTES,
            &outputConfig,
            &backgroundCapacity
        )
    );

    if (backgroundPixels == nullptr ||
        backgroundCapacity < BACKGROUND_RGB565_BYTES)
    {
        ESP_LOGE(TAG, "Cannot allocate %u-byte RGB565 background",
                 static_cast<unsigned>(BACKGROUND_RGB565_BYTES));
        free(backgroundPixels);
        backgroundPixels = nullptr;
        backgroundCapacity = 0;
        free(input);
        return false;
    }

    jpeg_decode_engine_cfg_t engineConfig = {};
    engineConfig.intr_priority = 0;
    engineConfig.timeout_ms = 1000;

    jpeg_decoder_handle_t decoder = nullptr;
    err = jpeg_new_decoder_engine(&engineConfig, &decoder);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Cannot create hardware JPEG decoder: %s",
                 esp_err_to_name(err));
        free(backgroundPixels);
        backgroundPixels = nullptr;
        backgroundCapacity = 0;
        free(input);
        return false;
    }

    jpeg_decode_cfg_t decodeConfig = {};
    decodeConfig.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
    // Matches the working wineglass decoder's pixel ordering.
    decodeConfig.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
    decodeConfig.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;

    uint32_t outputSize = 0;
    err = jpeg_decoder_process(
        decoder,
        &decodeConfig,
        input,
        static_cast<uint32_t>(fileSize),
        backgroundPixels,
        static_cast<uint32_t>(backgroundCapacity),
        &outputSize
    );

    jpeg_del_decoder_engine(decoder);
    free(input);

    if (err != ESP_OK || outputSize < BACKGROUND_RGB565_BYTES)
    {
        ESP_LOGE(TAG, "Background decode failed: %s (%u bytes)",
                 esp_err_to_name(err),
                 static_cast<unsigned>(outputSize));
        free(backgroundPixels);
        backgroundPixels = nullptr;
        backgroundCapacity = 0;
        return false;
    }

    memset(&backgroundDescriptor, 0, sizeof(backgroundDescriptor));
    backgroundDescriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    backgroundDescriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    backgroundDescriptor.header.w = BACKGROUND_WIDTH;
    backgroundDescriptor.header.h = BACKGROUND_HEIGHT;
    backgroundDescriptor.data_size = BACKGROUND_RGB565_BYTES;
    backgroundDescriptor.data = backgroundPixels;

    backgroundLoaded = true;
    ESP_LOGI(TAG, "Detail background loaded: %dx%d, JPEG=%ld bytes",
             BACKGROUND_WIDTH, BACKGROUND_HEIGHT, fileSize);
    return true;
}

void WineDetailsScreenDismiss()
{
    if (detailsScreen != nullptr)
    {
        lv_obj_set_hidden(detailsScreen, true);
    }
}

static void DetailsClicked(lv_event_t* event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }

    // Preserve the existing P4 -> S3 release protocol.
    esp_err_t result = DisplaySendStateChange(
        UART_NUM_1,
        "WAITINGFORBOTTLE"
    );

    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to transmit WAITINGFORBOTTLE");
        return;
    }

    ESP_LOGI(TAG, "WAITINGFORBOTTLE sent to S3");
    WineDetailsScreenDismiss();
}

void WineDetailsScreenShow(const WinePresentationData& wine)
{
    ESP_LOGI(TAG, "WineDetailsScreenShow ENTERED");

    lv_display_t* display = lv_display_get_default();
    if (display == nullptr)
    {
        return;
    }

    const int width = lv_display_get_horizontal_resolution(display);
    const int height = lv_display_get_vertical_resolution(display);
    screenSize = width < height ? width : height;

    // Create once, then reuse the same full-screen overlay.
    if (detailsScreen == nullptr)
    {
        detailsScreen = lv_obj_create(lv_layer_top());
        lv_obj_set_pos(detailsScreen, 0, 0);
        lv_obj_set_size(detailsScreen, width, height);
        lv_obj_set_style_radius(detailsScreen, 0, 0);
        lv_obj_set_style_pad_all(detailsScreen, 0, 0);
        lv_obj_set_style_border_width(detailsScreen, 0, 0);
        lv_obj_set_style_bg_color(
            detailsScreen, lv_color_hex(BACKGROUND), 0);
        lv_obj_set_style_bg_opa(detailsScreen, LV_OPA_COVER, 0);
        lv_obj_set_scrollable(detailsScreen, false);
        lv_obj_set_clickable(detailsScreen, true);
        lv_obj_add_event_cb(
            detailsScreen,
            DetailsClicked,
            LV_EVENT_CLICKED,
            nullptr
        );
    }
  else
    {
        lv_obj_clean(detailsScreen);
        wineLabelObject = nullptr;

        // Invalidate cached image data before freeing its pixels.
        lv_image_cache_drop(&displayedLabel.descriptor);

        ReleaseLabel(displayedLabel);
        ClearIconBuffers();
    }

    // Prepare all resources needed to display this wine.
    WineDetailsScreenPrepare(wine);    

    // First child is the background. All labels below are foreground.
    // The JPEG already has the gold bezel, cat/wordmark, static labels,
    // image placeholder frame, rating/value frames, and section headings.
    if (LoadBackgroundImage())
    {
        lv_obj_t* backgroundImage = lv_image_create(detailsScreen);
        lv_image_set_src(backgroundImage, &backgroundDescriptor);
        lv_obj_set_pos(
            backgroundImage,
            (width - screenSize) / 2,
            (height - screenSize) / 2
        );
        lv_image_set_pivot(backgroundImage, 0, 0);
        lv_image_set_scale(backgroundImage, screenSize * 256 / 800);
        lv_obj_set_clickable(backgroundImage, false);
        lv_obj_set_scrollable(backgroundImage, false);
        lv_obj_move_background(backgroundImage);
    }
    else
    {
        ESP_LOGW(TAG, "Background unavailable; using plain black overlay");
    }

    // Dynamic data only: positions now correspond to 800x800 JPEG artwork.
    // Keep data below the static headings baked into the image.

    // Dynamic data only: positions correspond to 800x800 JPEG artwork.
    // All presentation information comes from WinePresentationData.

    AddText(detailsScreen, 377, 166, 355,
            wine.producer.c_str(), WHITE, false, true);

    AddText(detailsScreen, 377, 222, 370,
            wine.wineName.c_str(), WHITE, false, true);

    if (!wine.cuvee.empty())
    {
        AddText(detailsScreen, 377, 282, 355, wine.cuvee.c_str(), WHITE, false, true);
    }

    // Retain the existing test artwork.
    // Dynamic rating selection comes later.
    AddRatingImage(detailsScreen, wine.ratingScore);
    AddWineLabImage(detailsScreen);
    // Display the actual Wine Labs rating over the rating artwork.
    if (wine.ratingScore >= 0.0)
    {
        char ratingText[16] = {};

        snprintf(ratingText, sizeof(ratingText), "%.0f (%d)", wine.ratingScore, wine.ratingReviewCount);
        AddText(detailsScreen, TEST_RATING_X + 20, TEST_RATING_Y + 20, 80, ratingText, GOLD, false, true);
    }

    // Right data frame

// Vintage heading - small gold lettering.
lv_obj_t* vintageHeading = AddText(detailsScreen, 650, 335, 230, "Vintage", GOLD, false, false);
lv_obj_set_style_text_font(vintageHeading, &lv_font_montserrat_12, 0);
AddText(detailsScreen, 650, 355, 230, wine.vintage.c_str(), WHITE, false, false);

// Vintage value - white lettering.
lv_obj_t* valueHeading = AddText(detailsScreen, 650, 390, 230, "Value", GOLD, false, false);
lv_obj_set_style_text_font(valueHeading, &lv_font_montserrat_12, 0);
AddText(detailsScreen, 650, 410, 230, wine.reportedValue.c_str(), WHITE, false, false);


if (wine.drinkingWindowStart > 0 && wine.drinkingWindowEnd > wine.drinkingWindowStart)
{
    const int startYear = wine.drinkingWindowStart;
    const int endYear = wine.drinkingWindowEnd;

    int currentYear = g_wineReadingYear.load();

    // Preserve the original timeline behavior when the
    // clock is valid. Otherwise begin at the drinking start.
    const int lowerYear = (currentYear < startYear)
            ? currentYear
            : startYear;

    const std::string lowerText = std::to_string(lowerYear);
    const std::string upperText = std::to_string(endYear);

    //
    // ALWAYS draw the drinking-window years.
    //

    AddText(detailsScreen, 77, 200, 60,
            upperText.c_str(), GOLD, false, true);

    AddText(detailsScreen, 77, 392, 60,
            lowerText.c_str(), GOLD, false, true);

    //
    // The progress bar requires a valid current year.
    //

        constexpr int BAR_X = 102;
        constexpr int BAR_TOP = 234;
        constexpr int BAR_BOTTOM = 385;
        constexpr int BAR_WIDTH = 10;

        const int barHeight = BAR_BOTTOM - BAR_TOP;
        const int totalYears = endYear - lowerYear;

        const int transitionYear =
            (startYear > currentYear) ? startYear : currentYear;

        int redYears = transitionYear - lowerYear;

        if (redYears < 0)
        {
            redYears = 0;
        }

        if (redYears > totalYears)
        {
            redYears = totalYears;
        }

        const int redHeight =
            (redYears * barHeight) / totalYears;

        const int greenHeight = barHeight - redHeight;

        constexpr uint32_t GREEN = 0x79B98F;
        constexpr uint32_t SOFT_RED = 0xCB827E;
        constexpr uint32_t DARK_RED = 0x903F49;

        auto DrawSegment = [&](int y, int height, uint32_t color)
        {
            if (height <= 0)
            {
                return;
            }

            lv_obj_t* segment = lv_obj_create(detailsScreen);

            lv_obj_set_pos(segment, S(BAR_X), S(y));
            lv_obj_set_size(segment, S(BAR_WIDTH), S(height));

            lv_obj_set_style_bg_color(
                segment, lv_color_hex(color), 0);

            lv_obj_set_style_bg_opa(segment, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(segment, 0, 0);
            lv_obj_set_style_radius(segment, 0, 0);
            lv_obj_set_style_pad_all(segment, 0, 0);

            lv_obj_set_clickable(segment, false);
            lv_obj_set_scrollable(segment, false);
        };

        // Green occupies the upper portion.
        DrawSegment(BAR_TOP, greenHeight, GREEN);

        // Red occupies the lower portion.
        if (redHeight > 0)
        {
            const uint32_t redColor =
                (startYear > currentYear) ? SOFT_RED : DARK_RED;

            DrawSegment(
                BAR_TOP + greenHeight,
                redHeight,
                redColor
            );
        }
}

    lv_obj_t* credit = AddText(detailsScreen, 238, 720, 320, "Critic ratings courtesy of Wine Labs", WHITE, true, false);
    lv_obj_set_style_text_font(credit, &lv_font_montserrat_12, 0);

    // Aroma and palate icons.
AddIconRow(
    detailsScreen,
    466,
    wine.aromas,
    wine.aromaIcons,
    0
);

AddIconRow(
    detailsScreen,
    574,
    wine.palate,
    wine.palateIcons,
    5
);
    // Prepare the wine label from the presentation data.
    if (!wine.labelJpeg.empty())
    {
        if (!WineDetailsScreenPrepareLabel(wine.labelJpeg.data(), wine.labelJpeg.size()))
        {
            ESP_LOGW(TAG, "Unable to prepare wine label");
        }
        else
        {
            WineDetailsScreenApplyLabel();
        }
    }


    lv_obj_set_hidden(detailsScreen, false);
    lv_obj_move_foreground(detailsScreen);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Wine Labs information panel.
//
static constexpr const char* WINELAB_IMAGE_PATH = "/spiffs/winelabdata.jpg";
static constexpr int WINELAB_X = 620;
static constexpr int WINELAB_Y = 315;
static constexpr int WINELAB_WIDTH = 120;
static constexpr int WINELAB_HEIGHT = 136;

static uint8_t* wineLabPixels = nullptr;
static lv_image_dsc_t wineLabDescriptor = {};
static bool wineLabLoaded = false;


///////////////////////////////////////////////////////////////////////////////////////////
// Load and decode the Wine Labs JPEG once.
//
static bool LoadWineLabImage()
{
    if (wineLabLoaded)
    {
        return true;
    }

    FILE* file = fopen(WINELAB_IMAGE_PATH, "rb");

    if (file == nullptr)
    {
        ESP_LOGE(TAG, "Cannot open Wine Labs image");
        return false;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    rewind(file);

    if (fileSize <= 0 || fileSize > 65536)
    {
        fclose(file);
        return false;
    }

    jpeg_decode_memory_alloc_cfg_t inputConfig = {};
    inputConfig.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER;

    size_t inputCapacity = 0;

    uint8_t* input = static_cast<uint8_t*>(
        jpeg_alloc_decoder_mem(
            fileSize,
            &inputConfig,
            &inputCapacity
        )
    );

    if (input == nullptr || inputCapacity < static_cast<size_t>(fileSize))
    {
        fclose(file);
        free(input);
        return false;
    }

    size_t bytesRead = fread(input, 1, fileSize, file);
    fclose(file);

    if (bytesRead != static_cast<size_t>(fileSize))
    {
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
        info.width != WINELAB_WIDTH ||
        info.height != WINELAB_HEIGHT)
    {
        ESP_LOGE(TAG, "Wine Labs JPEG must be 120x136");
        free(input);
        return false;
    }

    // Account for JPEG decoder padding.
    const size_t paddedWidth =
        (static_cast<size_t>(info.width) + 15) & ~size_t(15);

    const size_t paddedHeight =
        (static_cast<size_t>(info.height) + 15) & ~size_t(15);

    const size_t outputBytes = paddedWidth * paddedHeight * 2;
    const size_t visibleBytes =
        static_cast<size_t>(info.width) * info.height * 2;

    jpeg_decode_memory_alloc_cfg_t outputConfig = {};
    outputConfig.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;

    size_t outputCapacity = 0;

    uint8_t* pixels = static_cast<uint8_t*>(
        jpeg_alloc_decoder_mem(
            outputBytes,
            &outputConfig,
            &outputCapacity
        )
    );

    if (pixels == nullptr || outputCapacity < outputBytes)
    {
        free(input);
        free(pixels);
        return false;
    }

    jpeg_decode_engine_cfg_t engineConfig = {};
    engineConfig.intr_priority = 0;
    engineConfig.timeout_ms = 1000;

    jpeg_decoder_handle_t decoder = nullptr;

    err = jpeg_new_decoder_engine(&engineConfig, &decoder);

    if (err != ESP_OK)
    {
        free(input);
        free(pixels);
        return false;
    }

    jpeg_decode_cfg_t decodeConfig = {};
    decodeConfig.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
    decodeConfig.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
    decodeConfig.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;

    uint32_t decodedBytes = 0;

    err = jpeg_decoder_process(
        decoder,
        &decodeConfig,
        input,
        static_cast<uint32_t>(fileSize),
        pixels,
        static_cast<uint32_t>(outputCapacity),
        &decodedBytes
    );

    jpeg_del_decoder_engine(decoder);
    free(input);

    if (err != ESP_OK || decodedBytes < visibleBytes)
    {
        free(pixels);
        return false;
    }

    // Compact padded scanlines when necessary.
    size_t sourceStride = 0;

    if (decodedBytes == paddedWidth * paddedHeight * 2 ||
        decodedBytes == paddedWidth * static_cast<size_t>(info.height) * 2)
    {
        sourceStride = paddedWidth * 2;
    }
    else if (decodedBytes == static_cast<size_t>(info.width) * paddedHeight * 2 ||
             decodedBytes == visibleBytes)
    {
        sourceStride = static_cast<size_t>(info.width) * 2;
    }
    else
    {
        free(pixels);
        return false;
    }

    const size_t visibleStride = static_cast<size_t>(info.width) * 2;

    if (sourceStride != visibleStride)
    {
        for (size_t row = 1; row < info.height; ++row)
        {
            memmove(
                pixels + row * visibleStride,
                pixels + row * sourceStride,
                visibleStride
            );
        }
    }

    wineLabPixels = pixels;

    wineLabDescriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    wineLabDescriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    wineLabDescriptor.header.w = info.width;
    wineLabDescriptor.header.h = info.height;
    wineLabDescriptor.data_size = visibleBytes;
    wineLabDescriptor.data = wineLabPixels;

    wineLabLoaded = true;

    ESP_LOGI(TAG, "Wine Labs image decoded successfully");

    return true;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Display the Wine Labs information panel.
//
static void AddWineLabImage(lv_obj_t* parent)
{
    if (!LoadWineLabImage())
    {
        return;
    }

    lv_obj_t* image = lv_image_create(parent);

    lv_image_set_src(image, &wineLabDescriptor);
    lv_image_set_pivot(image, 0, 0);

    lv_obj_set_pos(
        image,
        S(WINELAB_X),
        S(WINELAB_Y)
    );

    lv_image_set_scale(image, screenSize * 256 / 800);

    lv_obj_set_clickable(image, false);
    lv_obj_set_scrollable(image, false);
}

void WineDetailsScreenSetCurrentYear(int year)
{
    if (year < 2020 || year > 2100)
    {
        return;
    }

    g_wineReadingYear.store(year);

    ESP_LOGI(TAG, "Wine reading calendar year: %d", year);
}

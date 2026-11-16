// winedetailscreen.cpp -- ESP32-P4
// Wine details overlay with a persistent, hardware-decoded JPEG background.
// Uses the exact screen entry points and tap-to-dismiss UART callback from
// the existing implementation. Call WineDetailsScreenShow with the LVGL lock held.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>

#include <driver/jpeg_decode.h>
#include <esp_err.h>
#include <esp_log.h>
#include <lvgl.h>

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
static constexpr size_t BACKGROUND_RGB565_BYTES =
    BACKGROUND_WIDTH * BACKGROUND_HEIGHT * 2;
static constexpr size_t MAX_BACKGROUND_JPEG_BYTES = 512 * 1024;
static constexpr const char* BACKGROUND_JPEG_PATH =
    "/spiffs/winedetailbackground.jpg";

// Keep the decoded pixels and LVGL descriptor alive between presentations.
// lv_obj_clean(detailsScreen) deletes children but NOT this image data.
static uint8_t* backgroundPixels = nullptr;
static size_t backgroundCapacity = 0;
static lv_image_dsc_t backgroundDescriptor = {};
static bool backgroundLoaded = false;

// New background is laid out at 800 x 800, not the former 720 x 720.
static int S(int value)
{
    return value * screenSize / 800;
}

static const lv_font_t* HeadingFont()
{
#if LV_FONT_MONTSERRAT_24
    return &lv_font_montserrat_24;
#else
    return LV_FONT_DEFAULT;
#endif
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

struct IconDefinition
{
    const char* id;
    const char* caption;
};

static constexpr IconDefinition ICON_DEFINITIONS[] =
{
    {"peach", "Peach"},
    {"apricot", "Apricot"},
    {"cherry", "Cherry"},
    {"blackcurrant", "Blackcurrant"},
    {"blackberry", "Blackberry"},
    {"plum", "Plum"},
    {"apple", "Apple"},
    {"pear", "Pear"},
    {"citrus", "Citrus"},
    {"brioche", "Brioche"},
    {"floral", "Floral"},
    {"violet", "Violet"},
    {"vanilla", "Vanilla"},
    {"cedar", "Cedar"},
    {"tobacco", "Tobacco"},
    {"leather", "Leather"},
    {"oak", "Oak"},
    {"spice", "Spice"},
    {"mineral", "Mineral"},
    {"honey", "Honey"},
    {"almond", "Almond"},
    {"chocolate", "Chocolate"},
    {"coffee", "Coffee"},
    {"dark_fruit", "Dark Fruit"},
    {"red_fruit", "Red Fruit"},
    {"creamy", "Creamy"},
    {"tannins", "Tannins"},
    {"acidity", "Acidity"},
    {"full_body", "Full Body"},
    {"crisp", "Crisp"},
    {"smooth", "Smooth"},
    {"long_finish", "Long Finish"},
};

static const IconDefinition* FindIcon(const char* keyword)
{
    if (keyword == nullptr)
    {
        return nullptr;
    }

    for (const IconDefinition& definition : ICON_DEFINITIONS)
    {
        if (strcmp(keyword, definition.id) == 0)
        {
            return &definition;
        }
    }

    return nullptr;
}

static bool LoadIcon(WineDetailIcon& destination, const char* keyword)
{
    // Only trusted, known filenames may be used to access SPIFFS.
    if (FindIcon(keyword) == nullptr)
    {
        ESP_LOGW(TAG, "Unrecognized wine icon: %s", keyword ? keyword : "(null)");
        return false;
    }

    char path[80] = {};
    snprintf(path, sizeof(path), "/spiffs/%s.jpg", keyword);

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
    ESP_LOGI(TAG, "Wine icon decoded: %s", keyword);
    return true;
}

static void AddIconRow(
    lv_obj_t* parent,
    int imageY,
    const char* const tags[5],
    int firstSlot)
{
    // Five columns across the content area, leaving margins for the round bezel.
//    static constexpr int COLUMN_CENTERS[5] = {160, 280, 400, 520, 640};
    static constexpr int COLUMN_CENTERS[5] = {200, 300, 400, 500, 600};

    for (int i = 0; i < 5; i++)
    {
        if (tags[i] == nullptr || tags[i][0] == '\0')
        {
            continue;
        }

        const IconDefinition* definition = FindIcon(tags[i]);
        if (definition == nullptr)
        {
            ESP_LOGW(TAG, "No icon mapping for keyword: %s", tags[i]);
            continue;
        }

        WineDetailIcon& icon = iconSlots[firstSlot + i];
        if (LoadIcon(icon, definition->id))
        {
            lv_obj_t* image = lv_image_create(parent);
            lv_image_set_src(image, &icon.descriptor);
            lv_image_set_pivot(image, 0, 0);
            lv_image_set_scale(image, ICON_DRAW_SIZE * 256 / ICON_FILE_SIZE * screenSize / 800);
            lv_obj_set_pos(image,
                           S(COLUMN_CENTERS[i] - ICON_DRAW_SIZE / 2),
                           S(imageY));
            lv_obj_set_clickable(image, false);
            lv_obj_set_scrollable(image, false);
        }

        AddText(parent, COLUMN_CENTERS[i] - 55, imageY + ICON_DRAW_SIZE + 2,
                110, definition->caption, WHITE, true);
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

// This function performs NO LVGL operations. Call it from the
// UART receiver task, WITHOUT the LVGL lock.
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

void WineDetailsScreenShow(const WineDetailsView& wine)
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
        ReleaseLabel(displayedLabel);
        ClearIconBuffers();
    }

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
    AddText(detailsScreen, 377, 166, 355,
            wine.producer, WHITE, false, true);

    AddText(detailsScreen, 377, 222, 370,
            wine.wineName, WHITE, false, true);

    if (wine.cuvee != nullptr && wine.cuvee[0] != '\0')
    {
        AddText(detailsScreen, 377, 282, 355,
                wine.cuvee, WHITE, false, true);
    }

    AddText(detailsScreen, 463, 318, 230,
            wine.vintage, WHITE, false, true);

    // Values inside the existing gold cards. No new LVGL frames.
    AddText(detailsScreen, 374, 387, 143,
            wine.rating, WHITE, true, true);

    AddText(detailsScreen, 536, 387, 190,
            wine.reportedValue, WHITE, true, true);

    // Five icons per row; adjust these Y coordinates to move each row.
    // Raising by 18 pixels gives the captions clearance above the next heading.
    AddIconRow(detailsScreen, 466, wine.aromas, 0);
    AddIconRow(detailsScreen, 574, wine.palate, 5);

    lv_obj_set_hidden(detailsScreen, false);
    lv_obj_move_foreground(detailsScreen);
}

// Retained from the supplied source for compatibility. This helper is
// not called by the working WinePresentationHandle path, which already
// constructs WineDetailsView from the received S3 message.
#include <cJSON.h>

static void ShowIdentifiedWine(const cJSON* payload)
{
    if (!cJSON_IsObject(payload))
    {
        return;
    }

    auto GetString = [payload](const char* key) -> const char*
    {
        const cJSON* item =
            cJSON_GetObjectItemCaseSensitive(payload, key);
        const char* value = cJSON_GetStringValue(item);
        return value != nullptr ? value : "";
    };

    std::string producer = GetString("winery");
    std::string fullWineName = GetString("wineName");
    std::string vintage = GetString("vintage");
    std::string wineName = fullWineName;

    if (!producer.empty() &&
        fullWineName.size() > producer.size() &&
        fullWineName.compare(0, producer.size(), producer) == 0 &&
        fullWineName[producer.size()] == ' ')
    {
        wineName = fullWineName.substr(producer.size() + 1);
    }

    WineDetailsView wine{};
    wine.producer = producer.c_str();
    wine.wineName = wineName.c_str();
    wine.vintage = vintage.c_str();
    wine.cuvee = "";
    wine.rating = "";
    wine.reportedValue = "";
    WineDetailsScreenShow(wine);
}

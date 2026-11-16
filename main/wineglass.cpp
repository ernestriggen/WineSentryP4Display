// wineglass.cpp

#include <cstdio>
#include <cstring>
#include <driver/jpeg_decode.h>
#include <esp_err.h>
#include <esp_log.h>

#include "displaywinedetailscreen.h"
#include "winefailurepopup.h"
#include "wineglass.h"

static const char* TAG = "WineGlass";

// ----------------------------------------------------------
// Image dimensions
// ----------------------------------------------------------

static constexpr int GLASS_WIDTH = 96;
static constexpr int GLASS_HEIGHT = 164;

// Visible image size: 96 x 164.

static constexpr size_t GLASS_BYTES = GLASS_WIDTH * GLASS_HEIGHT * 2;

// Hardware JPEG decoder requires 8-pixel height alignment.
// 164 pixels becomes 168 decoded rows.

static constexpr int GLASS_DECODE_HEIGHT = ((GLASS_HEIGHT + 7) / 8) * 8;

static constexpr size_t GLASS_DECODE_BYTES = GLASS_WIDTH * GLASS_DECODE_HEIGHT * 2;

static constexpr size_t JPEG_CAPACITY = 32 * 1024;

/////////////////////////////////////////////////////////////////
// ARC 3-2-1 constants
// Gold countdown arcs surrounding the wineglass.

static constexpr int ARC_DIAMETER = 120;
static constexpr int ARC_WIDTH = 8;
static constexpr uint32_t ARC_GOLD =  0xFF0000;  // Bright red // GOLD 0xD6AC58;

// White photographic illumination ring.

static constexpr int FLASH_DIAMETER = 790;
static constexpr int FLASH_WIDTH = 30;

// ----------------------------------------------------------
// Wineglass rotation
// ----------------------------------------------------------

static constexpr uint32_t SPIN_INTERVAL_MS = 40;

// LVGL uses tenths of a degree.
// 24 = 2.4 degrees every 40ms, or one revolution in 6 seconds.

static constexpr int SPIN_STEP = 24;

static lv_timer_t* wineGlassSpinTimer = nullptr;

static int wineGlassAngle = 0;
static int wineGlassDirection = 0;  // +1 clockwise, -1 counterclockwise

// Persistent LVGL objects.

static lv_obj_t* countdownArcs[3] = {};
static lv_obj_t* flashCircle = nullptr;
static bool flashRingLatched = false;

// ----------------------------------------------------------
// Dashboard coordinates
//
// Only ONE set of coordinates for ALL wineglass images.
// Initial position for the 800x800 dashboard.
// ----------------------------------------------------------

static constexpr int GLASS_X = 345;   // left in decrease
static constexpr int GLASS_Y = 625;   // down is increase

// LVGL 9: 256 = 100% size.
static constexpr int GLASS_SCALE = 125;  // 75%

// ----------------------------------------------------------
// Persistent image resources
// ----------------------------------------------------------

struct WineGlassImage
{
    uint8_t* buffer = nullptr;
    size_t capacity = 0;
    lv_image_dsc_t image = {};
};

static WineGlassImage grayGlass;
static WineGlassImage goldGlass;
static WineGlassImage redGlass;
static WineGlassImage smallRedGlass;
static WineGlassImage zoomRedGlass;

//////////////////////////////////////////////////////
// methods called but defined later
static void WineGlassCreateOverlays(lv_obj_t* screen);
static void WineGlassSetRotation(int direction);
static void WineGlassSpinTimerCallback(lv_timer_t* timer);

// ----------------------------------------------------------
// LVGL image object
// ----------------------------------------------------------

static lv_obj_t* wineGlassIcon = nullptr;

static const lv_image_dsc_t* currentGlassImage = nullptr;

// ----------------------------------------------------------
// Load and decode a JPEG from SPIFFS.
//
// Uses the same hardware decoder configuration as the
// existing lowerstatus.cpp implementation.
// ----------------------------------------------------------

static bool LoadWineGlassImage(
    WineGlassImage& destination,
    const char* path)
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
    rewind(file);

    if (fileSize <= 0 ||
        static_cast<size_t>(fileSize) > JPEG_CAPACITY)
    {
        ESP_LOGE(TAG, "Invalid JPEG size: %s", path);
        fclose(file);
        return false;
    }

    // Allocate input buffer.

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
        inputCapacity < static_cast<size_t>(fileSize))
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
        free(input);
        return false;
    }

    // Validate image dimensions.

    jpeg_decode_picture_info_t info = {};

    esp_err_t err = jpeg_decoder_get_info(
        input,
        static_cast<uint32_t>(fileSize),
        &info
    );

    if (err != ESP_OK ||
        info.width != GLASS_WIDTH ||
        info.height != GLASS_HEIGHT)
    {
        ESP_LOGE(
            TAG,
            "Expected 96x164 JPEG: %s",
            path
        );

        free(input);
        return false;
    }

    // Allocate persistent RGB565 image memory.

    jpeg_decode_memory_alloc_cfg_t outputConfig = {};

    outputConfig.buffer_direction =
        JPEG_DEC_ALLOC_OUTPUT_BUFFER;

destination.buffer = static_cast<uint8_t*>(
    jpeg_alloc_decoder_mem(
        GLASS_DECODE_BYTES,
        &outputConfig,
        &destination.capacity
    )
);
    if (destination.buffer == nullptr ||
        destination.capacity < GLASS_DECODE_BYTES)
    {
        ESP_LOGE(TAG, "JPEG output allocation failed");

        free(destination.buffer);
        destination.buffer = nullptr;
        destination.capacity = 0;

        free(input);
        return false;
    }

    // Create hardware decoder.

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

        free(destination.buffer);
        destination.buffer = nullptr;
        destination.capacity = 0;

        free(input);
        return false;
    }

    jpeg_decode_cfg_t decodeConfig = {};

    decodeConfig.output_format =
        JPEG_DECODE_OUT_FORMAT_RGB565;

    // Matches the verified color order in lowerstatus.cpp.

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
        destination.buffer,
        static_cast<uint32_t>(destination.capacity),
        &outputSize
    );

    jpeg_del_decoder_engine(decoder);
    free(input);

    if (err != ESP_OK || outputSize < GLASS_BYTES)
    {
        ESP_LOGE(TAG, "JPEG decode failed: %s", path);

        free(destination.buffer);
        destination.buffer = nullptr;
        destination.capacity = 0;

        return false;
    }

    // Prepare persistent LVGL image descriptor.

    memset(
        &destination.image,
        0,
        sizeof(destination.image)
    );

    destination.image.header.magic = LV_IMAGE_HEADER_MAGIC;
    destination.image.header.cf = LV_COLOR_FORMAT_RGB565;
    destination.image.header.w = GLASS_WIDTH;
    destination.image.header.h = GLASS_HEIGHT;

    destination.image.data_size = GLASS_BYTES;
    destination.image.data = destination.buffer;

    ESP_LOGI(TAG, "Wineglass loaded: %s", path);

    return true;
}

// ----------------------------------------------------------
// Select wineglass based on WINESTATE.
// ----------------------------------------------------------

static const lv_image_dsc_t* SelectWineGlass(WINESTATE state)
{
    switch (state)
    {
        // Camera not yet ready.

        case WAITINGFORAUTOFOCUS:
            return &grayGlass.image;

        // Camera ready, waiting for a bottle.

        case AUTOFOCUSCALIBRATED:
        case WAITINGFORBOTTLE:
        case SYSTEMIDLE:
            return &goldGlass.image;

        // Bottle too far from camera.

        case BOTTLETOOFAR:
            return &smallRedGlass.image;

        // Bottle too close to camera.

        case BOTTLETOOCLOSE:
            return &zoomRedGlass.image;

        // Bottle properly positioned.
        // Countdown arcs will be added separately.

        case BOTTLEJUSTRIGHT_STEP1:
        case BOTTLEJUSTRIGHT_STEP2:
        case BOTTLEJUSTRIGHT_STEP3:
        case BOTTLEJUSTRIGHT_ANDSTILL:
            return &redGlass.image;

        case BEGINPHOTOPROCESS:
            return &redGlass.image;

        case PHOTOTAKEN:
        case IDENTIFICATIONINPROG:
        case WINENOTFOUND:
        case WINEVINTAGEUNCERTAIN:
        case WINEFOUND:
        case TRANSMISSIONFAILURE:
            return &goldGlass.image;            

        // Everything else is temporarily gray.

        default:
            return &grayGlass.image;
    }
}

// ----------------------------------------------------------
// Initialize wineglass display.
// Called ONCE from dashboard initialization.
// ----------------------------------------------------------

bool WineGlassInitialize(lv_obj_t* screen)
{
    if (screen == nullptr)
        return false;

    if (wineGlassIcon != nullptr)
        return true;

    bool imagesOK = true;

    imagesOK = LoadWineGlassImage(
        grayGlass,
        "/spiffs/wineglassgrey.jpg"
    ) && imagesOK;

    imagesOK = LoadWineGlassImage(
        goldGlass,
        "/spiffs/wineglassgold.jpg"
    ) && imagesOK;

    imagesOK = LoadWineGlassImage(
        redGlass,
        "/spiffs/wineglassred.jpg"
    ) && imagesOK;

    imagesOK = LoadWineGlassImage(
        smallRedGlass,
        "/spiffs/wineglasstoofar.jpg"
    ) && imagesOK;

    imagesOK = LoadWineGlassImage(
        zoomRedGlass,
        "/spiffs/wineglasstooclose.jpg"
    ) && imagesOK;

    if (!imagesOK)
    {
        ESP_LOGE(TAG, "Wineglass image loading failed");
        return false;
    }

    // Create ONE persistent LVGL image object.

    wineGlassIcon = lv_image_create(screen);

    if (wineGlassIcon == nullptr)
    {
        ESP_LOGE(TAG, "Cannot create wineglass object");
        return false;
    }

    // The ONLY positioning operation.

    lv_obj_set_pos(wineGlassIcon, GLASS_X, GLASS_Y);

    // Scale from the upper-left corner.
    lv_image_set_pivot(wineGlassIcon, GLASS_WIDTH / 2, GLASS_HEIGHT / 2);

    lv_image_set_scale(wineGlassIcon, GLASS_SCALE);

    lv_obj_set_scrollable(wineGlassIcon, false);

    // Start with camera not ready.

    currentGlassImage = &grayGlass.image;

    lv_image_set_src(
        wineGlassIcon,
        currentGlassImage
    );

    WineGlassCreateOverlays(screen);

    // Create rotation timer, initially paused.

    wineGlassSpinTimer = lv_timer_create(WineGlassSpinTimerCallback, SPIN_INTERVAL_MS, nullptr);

    if (wineGlassSpinTimer == nullptr)
        {
        ESP_LOGE(TAG, "Wineglass rotation timer creation failed");
        return false;
        }

    lv_timer_pause(wineGlassSpinTimer);

    ESP_LOGI(TAG, "Wineglass initialized");

    return true;
}

// ----------------------------------------------------------
// Display wineglass based on WINESTATE.
//
// Only changes the image source.
// Coordinates never change.
// ----------------------------------------------------------

void WineGlassShow(WINESTATE state)
{
    if (wineGlassIcon == nullptr)
        return;

    // --------------------------------------------------
    // Select wineglass image.
    // --------------------------------------------------

    const lv_image_dsc_t* selectedImage =
        SelectWineGlass(state);

    if (selectedImage != currentGlassImage)
    {
        lv_image_set_src(
            wineGlassIcon,
            selectedImage
        );

        currentGlassImage = selectedImage;
    }

    // --------------------------------------------------
    // Determine countdown / flash state.
    // --------------------------------------------------

int arcsVisible = 0;

switch (state)
{
    case BOTTLEJUSTRIGHT_STEP1:
        arcsVisible = 1;
        break;

    case BOTTLEJUSTRIGHT_STEP2:
        arcsVisible = 2;
        break;

    case BOTTLEJUSTRIGHT_STEP3:
        arcsVisible = 3;
        break;

    case BOTTLEJUSTRIGHT_ANDSTILL:
        // Countdown complete.
        // Wait for BEGINPHOTOPROCESS before illuminating.
        break;

    case BEGINPHOTOPROCESS:
        // Keep the white ring illuminated throughout
        // autofocus and photograph capture.
        flashRingLatched = true;
        break;

    case PHOTOTAKEN:
        // Photograph captured.
        // User can now move the bottle.
        flashRingLatched = false;
        break;

    case IDENTIFICATIONINPROG:
    case WAITINGFORBOTTLE:
    case WAITINGFORBOTTLETOBEGONE:
        // Safety: make sure the ring is extinguished.
        flashRingLatched = false;
        break;

    case TRANSMISSIONFAILURE:
    {
        flashRingLatched = false;

        WineFailurePopupShow(
            "TRANSMISSION FAILURE",
            "The wine identification service did not respond.\n"
            "Please remove the bottle and try again."
        );

        break;
    }

    case WINENOTFOUND:
    {
        flashRingLatched = false;

        WineFailurePopupShow(
            "WINE NOT FOUND",
            "Unable to identify this wine.\n"
            "Please remove the bottle and try again."
        );

        break;
    }

    case WINEFOUND:
    {
        flashRingLatched = false;
        break;
    }

    case WINEVINTAGEUNCERTAIN:
        flashRingLatched = false;
        break;

    default:
        break;
}
    // --------------------------------------------------
    // Show the appropriate number of gold arcs.
    // --------------------------------------------------

    for (int i = 0; i < 3; i++)
    {
        if (countdownArcs[i] != nullptr)
        {
            lv_obj_set_hidden(
                countdownArcs[i],
                i >= arcsVisible
            );
        }
    }

    // --------------------------------------------------
    // Activate the white outer flash circle.
    // --------------------------------------------------

   if (flashCircle != nullptr)
{
    lv_obj_set_hidden(
        flashCircle,
        !flashRingLatched
    );

    if (flashRingLatched)
    {
        lv_obj_move_foreground(flashCircle);
    }
}

    // ----------------------------------------------------------
    // Wineglass rotation based on wine state.
    // ----------------------------------------------------------

    switch (state)
    {
        case BEGINPHOTOPROCESS:
            WineGlassSetRotation(+1);  // Clockwise
            break;

        case IDENTIFICATIONINPROG:
            WineGlassSetRotation(-1);  // Counterclockwise
            break;

        default:
            WineGlassSetRotation(0);   // Stop and return upright
            break;
    }    
}
///////////////////////////////////////////////////////////////
// arc and flash drawing

static void WineGlassCreateOverlays(lv_obj_t* screen)
{
    // Center of our wineglass image.
    // Assumes the image uses a centered scaling pivot.

    const int centerX = GLASS_X + GLASS_WIDTH / 2;
    const int centerY = GLASS_Y + GLASS_HEIGHT / 2;

    // Three segments forming an almost complete circle.
    // A small gap separates each segment.

    const int startAngles[3] = {240, 0, 120};
    const int endAngles[3]   = {350, 110, 230};

    for (int i = 0; i < 3; i++)
    {
        lv_obj_t* arc = lv_arc_create(screen);

        countdownArcs[i] = arc;

        if (arc == nullptr)
            continue;

        lv_obj_set_size(
            arc,
            ARC_DIAMETER,
            ARC_DIAMETER
        );

        lv_obj_set_pos(
            arc,
            centerX - ARC_DIAMETER / 2,
            centerY - ARC_DIAMETER / 2
        );

        lv_arc_set_bg_angles(arc, 0, 360);

        lv_arc_set_angles(
            arc,
            startAngles[i],
            endAngles[i]
        );

        // No visible background circle.

        lv_obj_set_style_arc_opa(
            arc,
            LV_OPA_TRANSP,
            LV_PART_MAIN
        );

        // Gold indicator segment.

        lv_obj_set_style_arc_color(
            arc,
            lv_color_hex(ARC_GOLD),
            LV_PART_INDICATOR
        );

        lv_obj_set_style_arc_width(
            arc,
            ARC_WIDTH,
            LV_PART_INDICATOR
        );

        lv_obj_set_style_arc_rounded(
            arc,
            true,
            LV_PART_INDICATOR
        );

        // No knob, touch behavior, or scrolling.

        lv_obj_remove_style(
            arc,
            nullptr,
            LV_PART_KNOB
        );

        lv_obj_set_clickable(arc, false);
        lv_obj_set_scrollable(arc, false);

        lv_obj_set_hidden(arc, true);
    }

    // --------------------------------------------------
    // White outer illumination ring.
    // Centered on the 800x800 round display.
    // --------------------------------------------------

    flashCircle = lv_obj_create(screen);

    if (flashCircle == nullptr)
        return;

    lv_obj_set_size(
        flashCircle,
        FLASH_DIAMETER,
        FLASH_DIAMETER
    );

    lv_obj_center(flashCircle);

    lv_obj_set_style_radius(
        flashCircle,
        LV_RADIUS_CIRCLE,
        0
    );

    lv_obj_set_style_bg_opa(
        flashCircle,
        LV_OPA_TRANSP,
        0
    );

    lv_obj_set_style_border_color(
        flashCircle,
        lv_color_white(),
        0
    );

    lv_obj_set_style_border_width(
        flashCircle,
        FLASH_WIDTH,
        0
    );

    lv_obj_set_style_border_opa(
        flashCircle,
        LV_OPA_COVER,
        0
    );

    lv_obj_set_style_pad_all(flashCircle, 0, 0);
    lv_obj_set_style_shadow_width(flashCircle, 0, 0);

    lv_obj_set_clickable(flashCircle, false);
    lv_obj_set_scrollable(flashCircle, false);

    lv_obj_set_hidden(flashCircle, true);

    ESP_LOGI(TAG, "Countdown arcs and flash circle created");
}

// ----------------------------------------------------------
// LVGL timer callback - rotate the wineglass.
// Executed by LVGL, so no additional display lock required.
// ----------------------------------------------------------

static void WineGlassSpinTimerCallback(lv_timer_t* timer)
{
    (void)timer;

    if (wineGlassIcon == nullptr || wineGlassDirection == 0)
        return;

    wineGlassAngle += wineGlassDirection * SPIN_STEP;

    if (wineGlassAngle >= 3600)
        wineGlassAngle -= 3600;

    if (wineGlassAngle < 0)
        wineGlassAngle += 3600;

    lv_image_set_rotation(wineGlassIcon, wineGlassAngle);
}


// ----------------------------------------------------------
// Start, reverse, or stop wineglass rotation.
// Caller must hold the LVGL display lock.
// ----------------------------------------------------------

static void WineGlassSetRotation(int direction)
{
    if (wineGlassIcon == nullptr || wineGlassSpinTimer == nullptr)
        return;

    wineGlassDirection = direction;

    if (direction == 0)
    {
        lv_timer_pause(wineGlassSpinTimer);

        wineGlassAngle = 0;

        lv_image_set_rotation(wineGlassIcon, 0);
    }
    else
    {
        lv_timer_resume(wineGlassSpinTimer);
    }
}
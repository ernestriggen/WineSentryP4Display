// rightstatus.cpp

#include "rightstatus.h"

#include <cstdio>
//#include <cstdlib>
#include <cstring>
//#include <string>

#include "bsp/esp-bsp.h"
#include "driver/jpeg_decode.h"
#include "esp_err.h"
#include "esp_log.h"
#include "statusbar.h"

static const char* TAG = "RightStatus";

// ------------------------------------------------------------
// Icon resources
// ------------------------------------------------------------

static constexpr int ICON_WIDTH = 64;
static constexpr int ICON_HEIGHT = 64;

static constexpr size_t ICON_BYTES =
    ICON_WIDTH * ICON_HEIGHT * 2;

static constexpr size_t JPEG_CAPACITY = 32 * 1024;

// ------------------------------------------------------------
// Display geometry
//
// All positions are relative to the 800x800 dashboard.
// These are initial coordinates for calibration.
// ------------------------------------------------------------

static constexpr int RIGHT_X = 585;
static constexpr int RIGHT_Y = 154;

static constexpr int BATTERY_ICON_X = 585;
static constexpr int BATTERY_ICON_Y = 320;

static constexpr int ICON_DISPLAY_SIZE = 70;

static constexpr int POWER_TEXT_X = BATTERY_ICON_X + ICON_DISPLAY_SIZE + 8;

static constexpr int POWER_TEXT_Y = BATTERY_ICON_Y + 24;

static constexpr int MODEL_TEXT_X = RIGHT_X + 8;
static constexpr int MODEL_TEXT_Y = RIGHT_Y + 67;

static constexpr int SENSITIVITY_TEXT_X = RIGHT_X + 8;
static constexpr int SENSITIVITY_TITLE_Y = RIGHT_Y + 110;
static constexpr int SENSITIVITY_VALUE_Y = RIGHT_Y + 132;
// WineSentry gold.

static constexpr uint32_t GOLD_COLOR = 0xD6AC58;

// ------------------------------------------------------------
// Image resources
// ------------------------------------------------------------

struct RightStatusImage
{
    uint8_t* buffer = nullptr;
    size_t capacity = 0;
    lv_image_dsc_t image = {};
};

static RightStatusImage battery4Image;
static RightStatusImage battery3Image;
static RightStatusImage battery2Image;
static RightStatusImage battery1Image;
static RightStatusImage acPowerImage;

// ------------------------------------------------------------
// Display objects
// ------------------------------------------------------------

static lv_obj_t* powerIcon = nullptr;
static lv_obj_t* powerLabel = nullptr;
static lv_obj_t* modelLabel = nullptr;

static lv_obj_t* sensitivityTitleLabel = nullptr;
static lv_obj_t* sensitivityLabel = nullptr;

static bool rightStatusInitialized = false;

// ------------------------------------------------------------
// Status model
// ------------------------------------------------------------

enum class PowerSource
{
    UNKNOWN,
    BATTERY,
    AC
};

static PowerSource powerSource = PowerSource::UNKNOWN;

static bool batteryKnown = false;
static int batteryPercent = 0;

// methods called but defined later
const char* GetSensorMarketingName(const char* sensorCode);

// ------------------------------------------------------------
// Hardware JPEG decoder
//
// Each 64x64 JPEG is decoded once into persistent RGB565
// memory. The original JPEG buffer is then released.
// ------------------------------------------------------------

static bool LoadRightStatusImage(
    RightStatusImage& destination,
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

    destination.image.header.magic =
        LV_IMAGE_HEADER_MAGIC;

    destination.image.header.cf =
        LV_COLOR_FORMAT_RGB565;

    destination.image.header.w = ICON_WIDTH;
    destination.image.header.h = ICON_HEIGHT;
    destination.image.header.stride = ICON_WIDTH * 2;

    destination.image.data_size = ICON_BYTES;
    destination.image.data = destination.buffer;

    ESP_LOGI(TAG, "Image decoded: %s", path);

    return true;
}

// ------------------------------------------------------------
// Create a text label
// ------------------------------------------------------------

static lv_obj_t* CreateStatusLabel(
    lv_obj_t* screen,
    int x,
    int y
)
{
    lv_obj_t* label = lv_label_create(screen);

    if (label == nullptr)
        return nullptr;

    lv_obj_set_pos(label, x, y);

    lv_obj_set_style_text_color(
        label,
        lv_color_hex(GOLD_COLOR),
        0
    );

    lv_obj_set_style_text_font(
        label,
        &lv_font_montserrat_18,
        0
    );

    lv_obj_set_scrollable(label, false);
    lv_obj_set_clickable(label, false);

    lv_label_set_text(label, "");

    return label;
}

// ------------------------------------------------------------
// Select battery icon
// ------------------------------------------------------------

static const lv_image_dsc_t* SelectBatteryImage()
{
    if (!batteryKnown)
        return nullptr;

    if (batteryPercent >= 76)
        return &battery4Image.image;

    if (batteryPercent >= 51)
        return &battery3Image.image;

    if (batteryPercent >= 26)
        return &battery2Image.image;

    return &battery1Image.image;
}


static const char* GetSensitivityDescription(int sensitivity)
{
    switch (sensitivity)
    {
        case 5:
            return "MAXIMUM";

        case 10:
            return "HIGH";

        case 15:
            return "MEDIUM";

        case 20:
            return "LOW";

        case 25:
            return "MINIMUM";

        default:
            return "??";
    }
}


// ------------------------------------------------------------
// Render status
//
// Caller must hold the LVGL display lock.
// ------------------------------------------------------------

static void RenderRightStatus()
{
    if (!rightStatusInitialized)
        return;

    // --------------------------------------------------------
    // Power source and battery level
    // --------------------------------------------------------

    if (powerSource == PowerSource::AC)
    {
        lv_image_set_src(
            powerIcon,
            &acPowerImage.image
        );

        lv_obj_set_hidden(powerIcon, false);

        lv_label_set_text(
            powerLabel,
            "AC Power"
        );
    }
    else if (
        powerSource == PowerSource::BATTERY &&
        batteryKnown
    )
    {
        const lv_image_dsc_t* image =
            SelectBatteryImage();

        if (image != nullptr)
        {
            lv_image_set_src(powerIcon, image);
            lv_obj_set_hidden(powerIcon, false);
        }

        char percentage[16];

        snprintf(
            percentage,
            sizeof(percentage),
            "%d%%",
            batteryPercent
        );

        lv_label_set_text(
            powerLabel,
            percentage
        );
    }
    else
    {
        // Power source or battery percentage is not known.
        // Do not invent a value.

        lv_obj_set_hidden(powerIcon, true);
        lv_label_set_text(powerLabel, "");
    }

    // --------------------------------------------------------
    // Sensor model
    // --------------------------------------------------------

// Sensor model comes directly from the status bar identity.

const char* model = StatusBarGetSensorModel();

if (model != nullptr && model[0] != '\0')
{
    char text[64];

    snprintf(
        text,
        sizeof(text),
        "%s", GetSensorMarketingName(model)
    );

    lv_label_set_text(modelLabel, text);
}
else
{
    lv_label_set_text(modelLabel, "");
}
    // --------------------------------------------------------
    // Sensitivity
    // --------------------------------------------------------


// --------------------------------------------------------
// Sensitivity
// --------------------------------------------------------

int sensitivity = StatusBarGetSensitivity();

const char* description = GetSensitivityDescription(sensitivity);

if (description[0] != '\0')
{
    lv_label_set_text(
        sensitivityTitleLabel,
        "Sensitivity"
    );

    lv_label_set_text(
        sensitivityLabel,
        description
    );
}
else
{
    lv_label_set_text(sensitivityTitleLabel, "");
    lv_label_set_text(sensitivityLabel, "");
}
}

// ------------------------------------------------------------
// Initialize right-side status display
//
// Caller must hold the LVGL display lock.
// ------------------------------------------------------------

bool RightStatusInit(lv_obj_t* screen)
{
    if (screen == nullptr)
        return false;

    if (rightStatusInitialized)
        return true;

    bool imagesOK = true;

    imagesOK = LoadRightStatusImage(
        battery4Image,
        "/spiffs/battery4.jpg"
    ) && imagesOK;

    imagesOK = LoadRightStatusImage(
        battery3Image,
        "/spiffs/battery3.jpg"
    ) && imagesOK;

    imagesOK = LoadRightStatusImage(
        battery2Image,
        "/spiffs/battery2.jpg"
    ) && imagesOK;

    imagesOK = LoadRightStatusImage(
        battery1Image,
        "/spiffs/battery1.jpg"
    ) && imagesOK;

    imagesOK = LoadRightStatusImage(
        acPowerImage,
        "/spiffs/acPower.jpg"
    ) && imagesOK;

    if (!imagesOK)
    {
        ESP_LOGE(
            TAG,
            "Right status image loading failed"
        );

        return false;
    }

    // --------------------------------------------------------
    // Power icon
    // --------------------------------------------------------

    powerIcon = lv_image_create(screen);

    if (powerIcon == nullptr)
        return false;

    lv_obj_set_pos(
        powerIcon,
    BATTERY_ICON_X,
    BATTERY_ICON_Y
    );

    static constexpr int IMAGE_SCALE =
        (ICON_DISPLAY_SIZE * 256) / ICON_WIDTH;

    lv_image_set_pivot(powerIcon, 0, 0);
    lv_image_set_scale(powerIcon, IMAGE_SCALE);

    lv_obj_set_clickable(powerIcon, false);
    lv_obj_set_hidden(powerIcon, true);

    // --------------------------------------------------------
    // Power percentage or AC Power
    // --------------------------------------------------------

    powerLabel = CreateStatusLabel(
        screen,
        POWER_TEXT_X,
        POWER_TEXT_Y
    );

    // --------------------------------------------------------
    // Sensor model
    // --------------------------------------------------------

    modelLabel = CreateStatusLabel(
        screen,
        MODEL_TEXT_X,
        MODEL_TEXT_Y
    );

    // --------------------------------------------------------
    // Sensitivity
    // --------------------------------------------------------


// --------------------------------------------------------
// Sensitivity - two lines
// --------------------------------------------------------

sensitivityTitleLabel = CreateStatusLabel(
    screen,
    SENSITIVITY_TEXT_X,
    SENSITIVITY_TITLE_Y
);

sensitivityLabel = CreateStatusLabel(
    screen,
    SENSITIVITY_TEXT_X,
    SENSITIVITY_VALUE_Y
);

    if (powerLabel == nullptr ||
        modelLabel == nullptr ||
        sensitivityTitleLabel == nullptr ||
        sensitivityLabel == nullptr)
    {
        ESP_LOGE(
            TAG,
            "Right status label creation failed"
        );

        return false;
    }

    rightStatusInitialized = true;

    RenderRightStatus();

    ESP_LOGI(
        TAG,
        "Right status initialized"
    );

    return true;
}

// ------------------------------------------------------------
// Set power source
// ------------------------------------------------------------

void RightStatusSetPowerSource(const char* source)
{
    if (source == nullptr)
        return;

    PowerSource newSource = powerSource;

    if (strcmp(source, "BATTERY") == 0 ||
        strcmp(source, "BAT") == 0)
    {
        newSource = PowerSource::BATTERY;
    }
    else if (strcmp(source, "AC") == 0 ||
             strcmp(source, "AC_POWER") == 0 ||
             strcmp(source, "MAINS") == 0)
    {
        newSource = PowerSource::AC;
    }
    else
    {
        return;
    }

    if (newSource == powerSource)
        return;

    if (!rightStatusInitialized)
    {
        powerSource = newSource;
        return;
    }

    if (bsp_display_lock(1000) != ESP_OK)
        return;

    powerSource = newSource;
    RenderRightStatus();

    bsp_display_unlock();
}

// ------------------------------------------------------------
// Set battery percentage
// ------------------------------------------------------------

void RightStatusSetBatteryPercent(int percent)
{
    if (percent < 0 || percent > 100)
        return;

    if (batteryKnown && batteryPercent == percent)
        return;

    if (!rightStatusInitialized)
    {
        batteryPercent = percent;
        batteryKnown = true;
        return;
    }

    if (bsp_display_lock(1000) != ESP_OK)
        return;

    batteryPercent = percent;
    batteryKnown = true;

    RenderRightStatus();

    bsp_display_unlock();
}

void RightStatusRender()
{
    RenderRightStatus();
}

const char* GetSensorMarketingName(const char* sensorCode)
{
    if (sensorCode == nullptr || strlen(sensorCode) != 2)
        return "Unknown Sensor";

    if (strcmp(sensorCode, "A1") == 0)
        return "InteGREAT";

    if (strcmp(sensorCode, "US") == 0)
        return "EchoGuard";

    if (strcmp(sensorCode, "PC") == 0)
        return "LumenGuard";

    if (strcmp(sensorCode, "AQ") == 0)
        return "AirSentry";

    if (strcmp(sensorCode, "TH") == 0)
        return "Ambiance";

    if (strcmp(sensorCode, "PR") == 0)
        return "ThermalGuard";

    if (strcmp(sensorCode, "FE") == 0)
        return "FlameWatch";

    if (strcmp(sensorCode, "H2") == 0)
        return "LeakWatch";

    if (strcmp(sensorCode, "WB") == 0)
        return "Grand Cru";

    if (strcmp(sensorCode, "wc") == 0)
        return "Premier";

    if (strcmp(sensorCode, "DW") == 0)
        return "AirVista";

    if (strcmp(sensorCode, "PG") == 0)
        return "PROPANE";

    if (strcmp(sensorCode, "er") == 0)
        return "ChronoChime";

    if (strcmp(sensorCode, "bp") == 0)
        return "BaroTrend";

    if (strcmp(sensorCode, "GS") == 0)
        return "Elite 9";

    if (strcmp(sensorCode, "A2") == 0)
        return "InteGREAT Xtra";

    if (strcmp(sensorCode, "DR") == 0)
        return "DriveSentry";

    if (strcmp(sensorCode, "pp") == 0)
        return "PowerPulse";

    if (strcmp(sensorCode, "XX") == 0)
        return "Sensor type not set";

    return "Unknown Sensor";
}

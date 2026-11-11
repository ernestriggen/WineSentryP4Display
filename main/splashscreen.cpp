// splashscreen.cpp

// A LOT OF WORK went into this splash.  Sure hope people love it.
// 100% of the code is chat

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>
#include <esp_err.h>
#include <esp_spiffs.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#include <driver/jpeg_decode.h>

#include <lvgl.h>
#include <bsp/esp-bsp.h>
#include <bsp/display.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "splashscreen.h"


static const char *TAG = "WineSentrySplash";

// ------------------------------------------------------------
// Animation configuration
// ------------------------------------------------------------

static constexpr int WIDTH = 800;
static constexpr int HEIGHT = 800;
static constexpr int FRAME_COUNT = 240;
static constexpr int FRAME_RATE = 26;

static constexpr size_t FRAME_BYTES =
    WIDTH * HEIGHT * 2;

static constexpr int64_t FRAME_INTERVAL_US =
    1000000LL / FRAME_RATE;

static constexpr size_t JPEG_INPUT_CAPACITY =
    256 * 1024;

// ------------------------------------------------------------
// Module-owned resources
// ------------------------------------------------------------

static uint8_t *jpeg_input = nullptr;
static size_t jpeg_input_capacity = 0;

static uint8_t *frame_buffers[2] = {nullptr, nullptr};
static size_t frame_capacities[2] = {0, 0};

static lv_image_dsc_t frame_images[2] = {};
static lv_obj_t *splash_object = nullptr;

static jpeg_decoder_handle_t decoder = nullptr;

// ------------------------------------------------------------
// SPIFFS
// ------------------------------------------------------------

static bool init_spiffs()
{
    esp_vfs_spiffs_conf_t conf = {};
    conf.base_path = "/spiffs";
    conf.partition_label = "storage";
    conf.max_files = 5;
    conf.format_if_mount_failed = false;

    esp_err_t err = esp_vfs_spiffs_register(&conf);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s",
                 esp_err_to_name(err));
        return false;
    }

    size_t total = 0;
    size_t used = 0;

    err = esp_spiffs_info("storage", &total, &used);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS info failed: %s",
                 esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "SPIFFS mounted: %u / %u bytes used",
             (unsigned)used, (unsigned)total);

    return true;
}

// ------------------------------------------------------------
// DMA-compatible hardware JPEG buffers
// ------------------------------------------------------------

static bool allocate_buffers()
{
    jpeg_decode_memory_alloc_cfg_t input_cfg = {};
    input_cfg.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER;

    jpeg_input = static_cast<uint8_t *>(
        jpeg_alloc_decoder_mem(
            JPEG_INPUT_CAPACITY,
            &input_cfg,
            &jpeg_input_capacity
        )
    );

    if (!jpeg_input ||
        jpeg_input_capacity < JPEG_INPUT_CAPACITY) {

        ESP_LOGE(TAG, "JPEG input allocation failed");
        return false;
    }

    jpeg_decode_memory_alloc_cfg_t output_cfg = {};
    output_cfg.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;

    for (int i = 0; i < 2; ++i) {

        frame_buffers[i] = static_cast<uint8_t *>(
            jpeg_alloc_decoder_mem(
                FRAME_BYTES,
                &output_cfg,
                &frame_capacities[i]
            )
        );

        if (!frame_buffers[i] ||
            frame_capacities[i] < FRAME_BYTES) {

            ESP_LOGE(TAG, "Frame buffer %d allocation failed", i);
            return false;
        }

        memset(frame_buffers[i], 0, FRAME_BYTES);

        frame_images[i].header.magic = LV_IMAGE_HEADER_MAGIC;
        frame_images[i].header.cf = LV_COLOR_FORMAT_RGB565;
        frame_images[i].header.w = WIDTH;
        frame_images[i].header.h = HEIGHT;
        frame_images[i].data_size = FRAME_BYTES;
        frame_images[i].data = frame_buffers[i];
    }

    ESP_LOGI(TAG, "Hardware JPEG buffers allocated");

    return true;
}

// ------------------------------------------------------------
// Hardware JPEG decoder initialization
// ------------------------------------------------------------

static bool initialize_decoder()
{
    jpeg_decode_engine_cfg_t cfg = {};
    cfg.intr_priority = 0;
    cfg.timeout_ms = 1000;

    esp_err_t err = jpeg_new_decoder_engine(&cfg, &decoder);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Hardware decoder initialization failed: %s",
                 esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "ESP32-P4 hardware JPEG decoder ready");

    return true;
}

// ------------------------------------------------------------
// Decode a JPEG frame from SPIFFS
// ------------------------------------------------------------

static bool decode_frame(
    int frame_number,
    int buffer_index,
    int64_t *decode_time_us
)
{
    char path[64];

    snprintf(path, sizeof(path),
             "/spiffs/frame_%03d.jpg", frame_number);

    FILE *file = fopen(path, "rb");

    if (!file) {
        ESP_LOGE(TAG, "Cannot open %s", path);
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }

    long file_size = ftell(file);
    rewind(file);

    if (file_size <= 0 ||
        static_cast<size_t>(file_size) > jpeg_input_capacity) {

        ESP_LOGE(TAG, "Invalid JPEG size at frame %d: %ld",
                 frame_number, file_size);

        fclose(file);
        return false;
    }

    size_t bytes_read = fread(
        jpeg_input,
        1,
        static_cast<size_t>(file_size),
        file
    );

    fclose(file);

    if (bytes_read != static_cast<size_t>(file_size)) {
        ESP_LOGE(TAG, "JPEG read failed at frame %d", frame_number);
        return false;
    }

    jpeg_decode_picture_info_t info = {};

    esp_err_t err = jpeg_decoder_get_info(
        jpeg_input,
        static_cast<uint32_t>(file_size),
        &info
    );

    if (err != ESP_OK ||
        info.width != WIDTH ||
        info.height != HEIGHT) {

        ESP_LOGE(TAG, "Invalid JPEG header at frame %d",
                 frame_number);
        return false;
    }

    jpeg_decode_cfg_t cfg = {};

    cfg.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;

    // IMPORTANT:
    // Verified correct color order on the WineSentry display.
    cfg.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR;

    cfg.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;

    uint32_t output_size = 0;

    int64_t start = esp_timer_get_time();

    err = jpeg_decoder_process(
        decoder,
        &cfg,
        jpeg_input,
        static_cast<uint32_t>(file_size),
        frame_buffers[buffer_index],
        static_cast<uint32_t>(frame_capacities[buffer_index]),
        &output_size
    );

    *decode_time_us = esp_timer_get_time() - start;

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "JPEG decode failed at frame %d: %s",
                 frame_number, esp_err_to_name(err));
        return false;
    }

    if (output_size < FRAME_BYTES) {
        ESP_LOGE(TAG, "Invalid decoded size at frame %d: %u",
                 frame_number, (unsigned)output_size);
        return false;
    }

    return true;
}

// ------------------------------------------------------------
// Display a decoded frame
// ------------------------------------------------------------

static void display_frame(int buffer_index)
{
    bsp_display_lock(-1);

    lv_image_set_src(
        splash_object,
        &frame_images[buffer_index]
    );

    lv_obj_invalidate(splash_object);

    bsp_display_unlock();
}

// ------------------------------------------------------------
// Transition from splash to application screen
// ------------------------------------------------------------

static void show_next_screen()
    {
    bsp_display_lock(-1);

    if (splash_object != nullptr)
        {
        lv_obj_delete(splash_object);
        splash_object = nullptr;
        }

    bsp_display_unlock();
    }
// ------------------------------------------------------------
// Animation playback
// ------------------------------------------------------------

static void play_animation()
{
    int64_t playback_start = esp_timer_get_time();

    int64_t total_decode_us = 0;
    int64_t maximum_decode_us = 0;

    int frames_displayed = 0;
    int buffer_index = 0;

    int64_t next_frame_time = playback_start;

    for (int frame = 1; frame <= FRAME_COUNT; ++frame) {

        int64_t decode_us = 0;

        if (!decode_frame(
                frame,
                buffer_index,
                &decode_us)) {

            ESP_LOGE(TAG, "Playback stopped at frame %d", frame);
            break;
        }

        total_decode_us += decode_us;

        if (decode_us > maximum_decode_us) {
            maximum_decode_us = decode_us;
        }

        int64_t now = esp_timer_get_time();

        // Preserve the existing slow-motion playback behavior.
        if (now < next_frame_time) {

            int64_t remaining = next_frame_time - now;

            if (remaining >= 1000) {
                vTaskDelay(
                    pdMS_TO_TICKS(
                        static_cast<uint32_t>(remaining / 1000)
                    )
                );
            }
        }

        display_frame(buffer_index);
        frames_displayed++;

        // The first splash frame has been submitted to LVGL.
        // Allow the display task to render it before
        // illuminating the LCD.
        if (frame == 1)
            {
            vTaskDelay(pdMS_TO_TICKS(100));

            bsp_display_backlight_on();

            ESP_LOGI(TAG, "Splash first frame ready; backlight enabled");
            }        

        if (frame == 1 || frame % 24 == 0) {
            ESP_LOGI(TAG,
                     "Frame %d/%d, hardware decode %lld ms",
                     frame,
                     FRAME_COUNT,
                     (long long)(decode_us / 1000));
        }

        // Yield to other FreeRTOS tasks.
        vTaskDelay(pdMS_TO_TICKS(1));

        next_frame_time += FRAME_INTERVAL_US;

        now = esp_timer_get_time();

        // Retain the timing behavior of our working version.
        if (next_frame_time < now) {
            next_frame_time = now + FRAME_INTERVAL_US;
        }

        buffer_index ^= 1;
    }

    int64_t elapsed =
        esp_timer_get_time() - playback_start;

    ESP_LOGI(TAG, "Playback complete");
    ESP_LOGI(TAG, "Frames displayed: %d", frames_displayed);
    ESP_LOGI(TAG, "Elapsed: %.2f seconds",
             elapsed / 1000000.0);

    ESP_LOGI(TAG, "Average hardware decode: %.2f ms",
             frames_displayed
                 ? total_decode_us / (1000.0 * frames_displayed)
                 : 0.0);

    ESP_LOGI(TAG, "Maximum hardware decode: %.2f ms",
             maximum_decode_us / 1000.0);

    // Hold the final splash frame before transitioning.
    vTaskDelay(pdMS_TO_TICKS(1000));

    show_next_screen();
}

// ------------------------------------------------------------
// Resource cleanup
// ------------------------------------------------------------

static void cleanup()
{
    if (decoder) {
        jpeg_del_decoder_engine(decoder);
        decoder = nullptr;
    }

    // The splash object is deleted before buffers are released.
    // Allow LVGL to process its pending rendering work.
    vTaskDelay(pdMS_TO_TICKS(100));

    for (int i = 0; i < 2; ++i) {
        if (frame_buffers[i]) {
            free(frame_buffers[i]);
            frame_buffers[i] = nullptr;
        }
        frame_capacities[i] = 0;
    }

    if (jpeg_input) {
        free(jpeg_input);
        jpeg_input = nullptr;
    }

    jpeg_input_capacity = 0;

    ESP_LOGI(TAG, "Splash resources released");
}

// ------------------------------------------------------------
// Public entry point
// ------------------------------------------------------------

void play_splash_screen()
{
    ESP_LOGI(TAG, "Starting WineSentry splash");

    if (!init_spiffs()) {
        return;
    }

    if (!allocate_buffers()) {
        ESP_LOGE(TAG, "Splash buffer initialization failed");
        cleanup();
        return;
    }

    if (!initialize_decoder()) {
        ESP_LOGE(TAG, "Splash decoder initialization failed");
        cleanup();
        return;
    }

    bsp_display_lock(-1);

    lv_obj_t *screen = lv_screen_active();

    lv_obj_set_style_bg_color(
        screen,
        lv_color_black(),
        0
    );

    splash_object = lv_image_create(screen);

    lv_obj_center(splash_object);

    bsp_display_unlock();

    play_animation();

    cleanup();

    ESP_LOGI(TAG, "WineSentry splash finished");
}

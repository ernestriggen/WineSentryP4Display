// winefailurepopup.cpp

#include "winefailurepopup.h"

#include <lvgl.h>
#include <esp_log.h>

static const char* TAG = "WineFailurePopup";

static constexpr uint32_t POPUP_DURATION_MS = 5000;

static constexpr uint32_t COLOR_GOLD = 0xD6AC58;
static constexpr uint32_t COLOR_PANEL = 0x181818;
static constexpr uint32_t COLOR_WHITE = 0xFFFFFF;
static constexpr uint32_t COLOR_GREY = 0xC0C0C0;

static lv_obj_t* popupOverlay = nullptr;
static lv_obj_t* popupTitle = nullptr;
static lv_obj_t* popupMessage = nullptr;

static lv_timer_t* popupTimer = nullptr;


// ----------------------------------------------------------
// Called by LVGL after five seconds.
// ----------------------------------------------------------

static void PopupTimeoutCallback(lv_timer_t* timer)
{
    (void)timer;

    if (popupOverlay != nullptr)
    {
        lv_obj_delete(popupOverlay);
    }

    popupOverlay = nullptr;
    popupTitle = nullptr;
    popupMessage = nullptr;

    // Our timer has a repeat count of one.
    // LVGL automatically deletes it after this callback.

    popupTimer = nullptr;

    ESP_LOGI(TAG, "Failure popup dismissed automatically");
}


// ----------------------------------------------------------
// Explicitly dismiss the popup.
// Must be called while holding the LVGL display lock.
// ----------------------------------------------------------

void WineFailurePopupDismiss()
{
    if (popupTimer != nullptr)
    {
        lv_timer_delete(popupTimer);
        popupTimer = nullptr;
    }

    if (popupOverlay != nullptr)
    {
        lv_obj_delete(popupOverlay);
        popupOverlay = nullptr;
    }

    popupTitle = nullptr;
    popupMessage = nullptr;
}


// ----------------------------------------------------------
// Display a modal failure popup for five seconds.
// Must be called while holding the LVGL display lock.
// ----------------------------------------------------------

void WineFailurePopupShow(
    const char* title,
    const char* message)
{
    if (title == nullptr || message == nullptr)
    {
        return;
    }

    // If already displayed, update the message
    // and restart the five-second countdown.

    if (popupOverlay != nullptr)
    {
        lv_label_set_text(popupTitle, title);
        lv_label_set_text(popupMessage, message);

        if (popupTimer != nullptr)
        {
            lv_timer_reset(popupTimer);
        }

        lv_obj_move_foreground(popupOverlay);

        return;
    }

    // ------------------------------------------------------
    // Modal overlay covering the entire active display.
    // ------------------------------------------------------

    popupOverlay = lv_obj_create(lv_layer_top());

    if (popupOverlay == nullptr)
    {
        return;
    }

    lv_obj_set_size(
        popupOverlay,
        LV_PCT(100),
        LV_PCT(100)
    );

    lv_obj_center(popupOverlay);

    lv_obj_set_style_radius(popupOverlay, 0, 0);
    lv_obj_set_style_pad_all(popupOverlay, 0, 0);
    lv_obj_set_style_border_width(popupOverlay, 0, 0);

    lv_obj_set_style_bg_color(
        popupOverlay,
        lv_color_black(),
        0
    );

    lv_obj_set_style_bg_opa(
        popupOverlay,
        LV_OPA_70,
        0
    );

    // Block touches to the dashboard underneath.

    lv_obj_set_clickable(popupOverlay, true);
    lv_obj_set_scrollable(popupOverlay, false);

    // ------------------------------------------------------
    // Centered message panel.
    // ------------------------------------------------------

    lv_obj_t* panel = lv_obj_create(popupOverlay);

    if (panel == nullptr)
    {
        WineFailurePopupDismiss();
        return;
    }

    lv_obj_set_size(panel, 600, 300);
    lv_obj_center(panel);

    lv_obj_set_style_bg_color(
        panel,
        lv_color_hex(COLOR_PANEL),
        0
    );

    lv_obj_set_style_bg_opa(
        panel,
        LV_OPA_COVER,
        0
    );

    lv_obj_set_style_radius(panel, 20, 0);

    lv_obj_set_style_border_color(
        panel,
        lv_color_hex(COLOR_GOLD),
        0
    );

    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);

    lv_obj_set_scrollable(panel, false);

    // ------------------------------------------------------
    // WineSentry branding.
    // ------------------------------------------------------

    lv_obj_t* brand = lv_label_create(panel);

    lv_label_set_text(brand, "W I N E S E N T R Y");

    lv_obj_set_style_text_font(
        brand,
        &lv_font_montserrat_14,
        0
    );

    lv_obj_set_style_text_color(
        brand,
        lv_color_hex(COLOR_GOLD),
        0
    );

    lv_obj_align(
        brand,
        LV_ALIGN_TOP_MID,
        0,
        35
    );

    // ------------------------------------------------------
    // Failure heading.
    // ------------------------------------------------------

    popupTitle = lv_label_create(panel);

    lv_label_set_text(popupTitle, title);

    lv_obj_set_style_text_font(
        popupTitle,
        &lv_font_montserrat_20,
        0
    );

    lv_obj_set_style_text_color(
        popupTitle,
        lv_color_hex(COLOR_WHITE),
        0
    );

    lv_obj_align(
        popupTitle,
        LV_ALIGN_TOP_MID,
        0,
        85
    );

    // ------------------------------------------------------
    // Gold divider.
    // ------------------------------------------------------

    lv_obj_t* divider = lv_obj_create(panel);

    lv_obj_set_size(divider, 280, 2);

    lv_obj_set_style_bg_color(
        divider,
        lv_color_hex(COLOR_GOLD),
        0
    );

    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);

    lv_obj_align(
        divider,
        LV_ALIGN_TOP_MID,
        0,
        127
    );

    // ------------------------------------------------------
    // Explanation.
    // ------------------------------------------------------

    popupMessage = lv_label_create(panel);

    lv_obj_set_width(popupMessage, 500);

    lv_label_set_long_mode(
        popupMessage,
        LV_LABEL_LONG_WRAP
    );

    lv_label_set_text(popupMessage, message);

    lv_obj_set_style_text_font(
        popupMessage,
        &lv_font_montserrat_20,
        0
    );

    lv_obj_set_style_text_color(
        popupMessage,
        lv_color_hex(COLOR_GREY),
        0
    );

    lv_obj_set_style_text_align(
        popupMessage,
        LV_TEXT_ALIGN_CENTER,
        0
    );

    lv_obj_set_style_text_line_space(
        popupMessage,
        8,
        0
    );

    lv_obj_align(
        popupMessage,
        LV_ALIGN_CENTER,
        0,
        45
    );

    lv_obj_move_foreground(popupOverlay);

    // ------------------------------------------------------
    // Five-second automatic dismissal.
    // ------------------------------------------------------

    popupTimer = lv_timer_create(
        PopupTimeoutCallback,
        POPUP_DURATION_MS,
        nullptr
    );

    if (popupTimer == nullptr)
    {
        ESP_LOGE(TAG, "Unable to create popup timer");
        WineFailurePopupDismiss();
        return;
    }

    lv_timer_set_repeat_count(popupTimer, 1);

    ESP_LOGI(TAG, "Failure popup displayed for five seconds");
}
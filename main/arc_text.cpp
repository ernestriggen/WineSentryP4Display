// arc_text.cpp

#include <cmath>
#include <cstring>

#include "arc_text.h"

constexpr float PI = 3.14159265358979323846f;
constexpr int CHARACTER_WIDTH = 24;
constexpr int CHARACTER_HEIGHT = 30;

void ArcTextInit(
    ArcText& arc,
    lv_obj_t* parent,
    int centerX,
    int centerY,
    int radius,
    float startDegrees,
    float endDegrees,
    const lv_font_t* font,
    uint32_t textColor)
{
    if (parent == nullptr || arc.count != 0)
        return;

    arc.centerX = centerX;
    arc.centerY = centerY;
    arc.radius = radius;
    arc.startDegrees = startDegrees;
    arc.endDegrees = endDegrees;
    arc.font = font;

    for (size_t i = 0; i < ARC_TEXT_MAX_CHARS; ++i)
    {
        lv_obj_t* label = lv_label_create(parent);

        lv_obj_set_size(label, CHARACTER_WIDTH, CHARACTER_HEIGHT);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(label, font, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(textColor), 0);
        lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
        lv_obj_set_style_transform_pivot_x(label, CHARACTER_WIDTH / 2, 0);
        lv_obj_set_style_transform_pivot_y(label, CHARACTER_HEIGHT / 2, 0);
        lv_obj_remove_flag(label, LV_OBJ_FLAG_SCROLLABLE);
        lv_label_set_text(label, "");
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        arc.characters[i] = label;
    }

    arc.count = ARC_TEXT_MAX_CHARS;
}

void ArcTextSetText(ArcText& arc, const char* text)
{
    if (arc.count == 0)
        return;

    // This renderer accepts printable ASCII (the timestamp is ASCII).
    // Allocate/position once; only label contents change for new readings.
    size_t length = text == nullptr ? 0 : std::strlen(text);

    if (length > arc.count)
        length = arc.count;

    for (size_t i = 0; i < arc.count; ++i)
    {
        lv_obj_t* label = arc.characters[i];

        if (i >= length)
        {
            lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        float fraction = length > 1
            ? static_cast<float>(i) / static_cast<float>(length - 1)
            : 0.5f;

        float degrees = arc.endDegrees +
            fraction * (arc.startDegrees - arc.endDegrees);

        float radians = degrees * PI / 180.0f;
        int x = static_cast<int>(std::lround(
            arc.centerX + arc.radius * std::cos(radians)));
        int y = static_cast<int>(std::lround(
            arc.centerY + arc.radius * std::sin(radians)));

        // Tangent to the circle, in the direction the text progresses.
        float tangent = degrees +
            (arc.endDegrees < arc.startDegrees ? 90.0f : -90.0f);

        char character[2] = {text[i], '\0'};
        lv_label_set_text(label, character);
        lv_obj_set_pos(
            label,
            x - CHARACTER_WIDTH / 2,
            y - CHARACTER_HEIGHT / 2);
        lv_obj_set_style_transform_rotation(
            label,
            static_cast<int>(std::lround(tangent * 10.0f)),
            0);
        lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}

void ArcTextSetColor(ArcText& arc, uint32_t textColor)
{
    for (size_t i = 0; i < arc.count; ++i)
        lv_obj_set_style_text_color(
            arc.characters[i],
            lv_color_hex(textColor),
            0);
}

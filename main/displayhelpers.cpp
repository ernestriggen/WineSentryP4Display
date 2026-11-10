#include "displayhelpers.h"

#include <math.h>

    constexpr int DESIGN = 720;
    lv_obj_t* frameScreen = nullptr;
    lv_obj_t* accentRing = nullptr;
    lv_obj_t* decorScreen = nullptr;

    int S(lv_obj_t* screen, int v)
        {
        int w = lv_obj_get_width(screen);
        int h = lv_obj_get_height(screen);
        int diameter = w < h ? w : h;
        return (v * diameter + DESIGN / 2) / DESIGN;
        }

    lv_color_t Gold() { return lv_color_hex(0xD6AC58); }
    lv_color_t BrightGold() { return lv_color_hex(0xF3D98D); }
    lv_color_t DarkGold() { return lv_color_hex(0x6B4B20); }

    lv_obj_t* Circle(lv_obj_t* parent, int diameter, int stroke, lv_color_t color)
        {
        lv_obj_t* obj = lv_obj_create(parent);
        lv_obj_remove_style_all(obj);
        lv_obj_set_size(obj, S(parent, diameter), S(parent, diameter));
        lv_obj_center(obj);
        lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(obj, S(parent, stroke), 0);
        lv_obj_set_style_border_color(obj, color, 0);
        lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        return obj;
        }

    void Line(lv_obj_t* parent, int x1, int y1, int x2, int y2,
              lv_color_t color, int width = 1)
        {
        // LVGL line stores a pointer to points: use two small anchored objects
        // instead, so no temporary point-array lifetime issue is possible.
        if (y1 == y2 || x1 == x2)
            {
            lv_obj_t* line = lv_obj_create(parent);
            lv_obj_remove_style_all(line);
            int left = x1 < x2 ? x1 : x2;
            int top = y1 < y2 ? y1 : y2;
            int w = x1 == x2 ? width : x2 - x1;
            int h = y1 == y2 ? width : y2 - y1;
            lv_obj_set_pos(line, S(parent, left), S(parent, top));
            lv_obj_set_size(line, S(parent, w), S(parent, h));
            lv_obj_set_style_bg_color(line, color, 0);
            lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
            lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
            }
        }

    void Label(lv_obj_t* parent, const char* text, int x, int y, int w,
               const lv_font_t* font, lv_color_t color)
        {
        lv_obj_t* label = lv_label_create(parent);
        lv_label_set_text(label, text);
        lv_obj_set_width(label, S(parent, w));
        lv_obj_set_pos(label, S(parent, x), S(parent, y));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(label, font, 0);
        lv_obj_set_style_text_color(label, color, 0);
        }

    void WineGlass(lv_obj_t* parent)
        {
        // Stylized glass: oval bowl, stem and base; all vector LVGL objects.
        lv_obj_t* bowl = lv_obj_create(parent);
        lv_obj_remove_style_all(bowl);
        lv_obj_set_size(bowl, S(parent, 25), S(parent, 34));
        lv_obj_set_pos(bowl, S(parent, 348), S(parent, 627));
        lv_obj_set_style_radius(bowl, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(bowl, S(parent, 2), 0);
        lv_obj_set_style_border_color(bowl, BrightGold(), 0);
        lv_obj_set_style_bg_opa(bowl, LV_OPA_TRANSP, 0);
        Line(parent, 360, 661, 360, 676, BrightGold(), 2);
        Line(parent, 348, 677, 372, 677, BrightGold(), 2);
        }

void DisplayHelpersReset()
    {
    frameScreen = nullptr;
    accentRing = nullptr;
    decorScreen = nullptr;
    }

void DisplayDrawFrame(lv_obj_t* screen, lv_color_t accent)
    {
    if (screen == nullptr)
        return;
    if (frameScreen == screen)
        {
        DisplaySetFrameAccent(accent);
        return;
        }

    frameScreen = screen;
    accentRing = nullptr;
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    // The rings sit inside the actual LCD boundary, not outside the glass.
    accentRing = Circle(screen, 708, 6, accent);
    Circle(screen, 695, 8, DarkGold());
    Circle(screen, 682, 5, Gold());
    Circle(screen, 670, 2, BrightGold());
    Circle(screen, 652, 2, DarkGold());
    }

void DisplaySetFrameAccent(lv_color_t accent)
    {
    if (accentRing != nullptr)
        lv_obj_set_style_border_color(accentRing, accent, 0);
    }

void DisplayDrawDecorations(lv_obj_t* screen, const lv_image_dsc_t* logoImage)
    {
    if (screen == nullptr || decorScreen == screen)
        return;
    decorScreen = screen;

    // Upper identity zone. Supply the existing crowned-cat image descriptor.
    if (logoImage != nullptr)
        {
        lv_obj_t* cat = lv_image_create(screen);
        lv_image_set_src(cat, logoImage);
        lv_obj_align(cat, LV_ALIGN_TOP_MID, 0, S(screen, 79));
        }
    Label(screen, "W I N E S E N T R Y", 210, 162, 300,
          &lv_font_montserrat_20, BrightGold());

    // Section dividers match the reference's three-column instrument layout.
    Line(screen, 105, 210, 615, 210, Gold(), 2);
    Line(screen, 95, 414, 625, 414, Gold(), 2);
    Line(screen, 96, 451, 624, 451, DarkGold(), 1);
    Line(screen, 292, 229, 292, 403, DarkGold(), 1);
    Line(screen, 510, 229, 510, 403, DarkGold(), 1);
    Line(screen, 110, 588, 610, 588, Gold(), 2);

    // Wineglass medallion at six o'clock.
    lv_obj_t* medallion = lv_obj_create(screen);
    lv_obj_remove_style_all(medallion);
    lv_obj_set_size(medallion, S(screen, 88), S(screen, 88));
    lv_obj_set_pos(medallion, S(screen, 316), S(screen, 609));
    lv_obj_set_style_radius(medallion, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(medallion, S(screen, 3), 0);
    lv_obj_set_style_border_color(medallion, Gold(), 0);
    lv_obj_set_style_bg_opa(medallion, LV_OPA_TRANSP, 0);
    WineGlass(screen);
    }

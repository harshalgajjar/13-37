/*
 * Swipeable watch-face picker — see face_picker_screen.h.
 */
#include "face_picker_screen.h"
#include "ui_haptic.h"
#include "settings_screen.h"
#include "wayfinder_face.h"
#include <lvgl.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *FACE_NAMES[3] = { "Digital", "Analog", "Wayfinder" };

static lv_obj_t *picker_screen  = nullptr;
static lv_obj_t *tileview       = nullptr;
static lv_obj_t *tiles[3]       = { nullptr, nullptr, nullptr };
static lv_obj_t *dots[3]        = { nullptr, nullptr, nullptr };
static lv_obj_t *cur_badge[3]   = { nullptr, nullptr, nullptr };
static lv_obj_t *select_lbl     = nullptr;
static int       active_index   = 0;
static int       current_face   = 0;   // the face actually in use (persisted)

static void update_select_label(void)
{
    if (select_lbl) lv_label_set_text_fmt(select_lbl, "USE %s", FACE_NAMES[active_index]);
}

/* --- small analog-clock emblem for the Analog preview tile --- */
static void analog_emblem_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t   *obj   = lv_event_get_target_obj(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    int cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
    int r  = (lv_area_get_width(&a) / 2) - 4;

    for (int i = 0; i < 12; i++) {
        double ang = (i * 30 - 90) * M_PI / 180.0;
        lv_draw_line_dsc_t d;
        lv_draw_line_dsc_init(&d);
        d.color = lv_color_white();
        d.width = 2;
        d.opa = LV_OPA_70;
        d.p1.x = cx + (lv_value_precise_t)(cos(ang) * r);
        d.p1.y = cy + (lv_value_precise_t)(sin(ang) * r);
        d.p2.x = cx + (lv_value_precise_t)(cos(ang) * (r - 10));
        d.p2.y = cy + (lv_value_precise_t)(sin(ang) * (r - 10));
        lv_draw_line(layer, &d);
    }
    /* hour hand ~10:10 look */
    struct { double deg; int len; lv_color_t c; int w; } hands[2] = {
        { -60,  r - 44, lv_color_white(),            4 },
        {  30,  r - 24, lv_color_hex(0xff7b2e),      3 },
    };
    for (int h = 0; h < 2; h++) {
        double ang = (hands[h].deg) * M_PI / 180.0;
        lv_draw_line_dsc_t d;
        lv_draw_line_dsc_init(&d);
        d.color = hands[h].c;
        d.width = hands[h].w;
        d.round_start = d.round_end = 1;
        d.p1.x = cx; d.p1.y = cy;
        d.p2.x = cx + (lv_value_precise_t)(cos(ang) * hands[h].len);
        d.p2.y = cy + (lv_value_precise_t)(sin(ang) * hands[h].len);
        lv_draw_line(layer, &d);
    }
}

/* --- build one tile: preview on top, name below --- */
static void build_tile(int idx, lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tile, 22, 0);

    const int D = 210;
    if (idx == 0) {                                   /* Digital */
        lv_obj_t *box = lv_obj_create(tile);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, D, D);
        lv_obj_t *t = lv_label_create(box);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(t, lv_color_white(), 0);
        lv_label_set_text(t, "10:42");
        lv_obj_center(t);
    } else if (idx == 1) {                            /* Analog */
        lv_obj_t *box = lv_obj_create(tile);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, D, D);
        lv_obj_set_style_radius(box, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(box, 2, 0);
        lv_obj_set_style_border_color(box, lv_color_hex(0x333333), 0);
        lv_obj_add_event_cb(box, analog_emblem_draw, LV_EVENT_DRAW_MAIN, NULL);
    } else {                                          /* Wayfinder */
        wayfinder_build_preview(tile, D);
    }

    lv_obj_t *name = lv_label_create(tile);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(name, lv_color_white(), 0);
    lv_label_set_text(name, FACE_NAMES[idx]);

    // "In use" badge — shown on whichever face is currently active, so the user
    // can see their current choice at a glance while swiping.
    cur_badge[idx] = lv_label_create(tile);
    lv_obj_set_style_text_font(cur_badge[idx], &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(cur_badge[idx], lv_color_hex(0xff7b2e), 0);
    lv_label_set_text(cur_badge[idx], LV_SYMBOL_OK "  IN USE");
    lv_obj_add_flag(cur_badge[idx], LV_OBJ_FLAG_HIDDEN);
}

static void refresh_current_badge(void)
{
    for (int i = 0; i < 3; i++) {
        if (!cur_badge[i]) continue;
        if (i == current_face) lv_obj_clear_flag(cur_badge[i], LV_OBJ_FLAG_HIDDEN);
        else                   lv_obj_add_flag(cur_badge[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_dots(void)
{
    for (int i = 0; i < 3; i++)
        lv_obj_set_style_bg_color(dots[i],
            i == active_index ? lv_color_hex(0xff7b2e) : lv_color_hex(0x444444), 0);
}

static void on_tile_changed(lv_event_t *e)
{
    lv_obj_t *act = lv_tileview_get_tile_active(tileview);
    for (int i = 0; i < 3; i++) if (tiles[i] == act) active_index = i;
    refresh_dots();
    update_select_label();
}

static void on_select(lv_event_t *e)
{
    ui_haptic_tap();                 // confirm buzz
    settings_apply_face(active_index);
    settings_screen_show();
}

static void on_back(lv_event_t *e)
{
    settings_screen_show();
}

static void build_screen(void)
{
    picker_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(picker_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(picker_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(picker_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(picker_screen);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xaaaaaa), 0);
    lv_label_set_text(title, "WATCH FACE");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 22);

    tileview = lv_tileview_create(picker_screen);
    lv_obj_set_size(tileview, 410, 340);
    lv_obj_align(tileview, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_opa(tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);
    for (int i = 0; i < 3; i++) {
        tiles[i] = lv_tileview_add_tile(tileview, i, 0, LV_DIR_HOR);
        build_tile(i, tiles[i]);
    }
    lv_obj_add_event_cb(tileview, on_tile_changed, LV_EVENT_VALUE_CHANGED, NULL);

    /* page dots */
    lv_obj_t *dotrow = lv_obj_create(picker_screen);
    lv_obj_remove_style_all(dotrow);
    lv_obj_set_size(dotrow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(dotrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(dotrow, 10, 0);
    lv_obj_align(dotrow, LV_ALIGN_TOP_MID, 0, 410);
    for (int i = 0; i < 3; i++) {
        dots[i] = lv_obj_create(dotrow);
        lv_obj_remove_style_all(dots[i]);
        lv_obj_set_size(dots[i], 10, 10);
        lv_obj_set_style_radius(dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dots[i], LV_OPA_COVER, 0);
    }

    /* SELECT + BACK */
    lv_obj_t *select_btn = lv_button_create(picker_screen);
    lv_obj_set_size(select_btn, 200, 54);
    lv_obj_align(select_btn, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_bg_color(select_btn, lv_color_hex(0xff7b2e), 0);
    lv_obj_set_style_radius(select_btn, 14, 0);
    lv_obj_add_event_cb(select_btn, on_select, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(select_btn);
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(sl, lv_color_black(), 0);
    lv_label_set_text(sl, "SELECT");
    select_lbl = sl;
    lv_obj_center(sl);

    lv_obj_t *back_btn = lv_button_create(picker_screen);
    lv_obj_set_size(back_btn, 90, 44);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 16, -22);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(back_btn, 12, 0);
    lv_obj_add_event_cb(back_btn, on_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back_btn);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(bl, lv_color_white(), 0);
    lv_label_set_text(bl, LV_SYMBOL_LEFT);
    lv_obj_center(bl);
}

void face_picker_show(int current_mode)
{
    if (current_mode < 0) current_mode = 0;
    if (current_mode > 2) current_mode = 2;
    if (!picker_screen) build_screen();
    active_index = current_mode;
    current_face = current_mode;
    lv_tileview_set_tile_by_index(tileview, current_mode, 0, LV_ANIM_OFF);
    refresh_dots();
    refresh_current_badge();
    update_select_label();
    // Slide in from the right — a "drill-in" transition (250ms, only on nav).
    lv_scr_load_anim(picker_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
}

#include "time_screen.h"
#include "alarm_screen.h"
#include "stopwatch_screen.h"
#include "timer_screen.h"
#include "calendar_screen.h"
#include <LilyGoLib.h>

// Defined in main.cpp
void clock_screen_show();

static lv_obj_t *time_screen;

// Swipe DOWN to return to the clock face — mirrors the swipe-UP entry from
// the clock. Other directions are no-ops so a sloppy left/right swipe inside
// a tile doesn't accidentally page away.
static void on_gesture(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_BOTTOM)
        clock_screen_show();
}

// ---- Tile helper -----------------------------------------------------------
// Identical to the tools-screen helper so the two grids feel uniform: a 180×
// 180 dark card with a bottom-anchored label and the icon drawn on top via
// LVGL primitives.
static lv_obj_t *make_tile(lv_obj_t *parent, const char *label_text)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_set_size(tile, 180, 180);
    lv_obj_set_style_bg_color(tile, lv_color_make(0x11, 0x11, 0x11), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(tile, lv_color_make(0x44, 0x44, 0x44), LV_PART_MAIN);
    lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(tile, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tile, 0, LV_PART_MAIN);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *lbl = lv_label_create(tile);
    lv_obj_set_style_text_color(lbl, lv_color_make(0xCC, 0xCC, 0xCC), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_label_set_text(lbl, label_text);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -12);

    return tile;
}

// ---- Icons -----------------------------------------------------------------
//
// Twin-bell alarm clock with splayed legs, striker bar across the bells, and
// hands that read roughly "alarm time".
// ---- Polished line-art icons ----------------------------------------------
// Monochrome light-grey stroke with a single restrained orange accent.
#define ICO_INK     lv_color_hex(0xE8E8EA)
#define ICO_DIM     lv_color_hex(0x6E6E73)
#define ICO_ACCENT  lv_color_hex(0xFF7B2E)

static void ico_ring(lv_obj_t *p, int cx, int cy, int d, int w, lv_color_t c)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_pos(o, cx - d / 2, cy - d / 2);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(o, w, 0);
    lv_obj_set_style_border_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static void ico_line(lv_obj_t *p, int x1, int y1, int x2, int y2, int w, lv_color_t c)
{
    lv_point_precise_t *pts = (lv_point_precise_t *)lv_malloc(sizeof(lv_point_precise_t) * 2);
    pts[0].x = x1; pts[0].y = y1; pts[1].x = x2; pts[1].y = y2;
    lv_obj_t *l = lv_line_create(p);
    lv_line_set_points(l, pts, 2);
    lv_obj_set_style_line_width(l, w, 0);
    lv_obj_set_style_line_color(l, c, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
}

static void ico_dot(lv_obj_t *p, int cx, int cy, int d, lv_color_t c)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_pos(o, cx - d / 2, cy - d / 2);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static void ico_rrect(lv_obj_t *p, int cx, int cy, int w, int h, int r, int sw, lv_color_t c, bool fill)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, cx - w / 2, cy - h / 2);
    lv_obj_set_style_radius(o, r, 0);
    if (fill) {
        lv_obj_set_style_bg_color(o, c, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(o, sw, 0);
        lv_obj_set_style_border_color(o, c, 0);
    }
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static void draw_alarm_icon(lv_obj_t *tile)
{
    const int cx = 90, cy = 76, R = 30;
    ico_ring(tile, cx, cy, R * 2, 3, ICO_INK);
    ico_line(tile, cx - 21, cy - 21, cx - 30, cy - 30, 4, ICO_INK);
    ico_line(tile, cx + 21, cy - 21, cx + 30, cy - 30, 4, ICO_INK);
    ico_line(tile, cx - 20, cy + 22, cx - 28, cy + 32, 3, ICO_INK);
    ico_line(tile, cx + 20, cy + 22, cx + 28, cy + 32, 3, ICO_INK);
    ico_line(tile, cx, cy, cx, cy - 18, 3, ICO_INK);
    ico_line(tile, cx, cy, cx + 13, cy + 6, 3, ICO_ACCENT);
    ico_dot(tile, cx, cy, 6, ICO_INK);
}

static void draw_stopwatch_icon(lv_obj_t *tile)
{
    const int cx = 90, cy = 80, R = 30;
    ico_ring(tile, cx, cy, R * 2, 3, ICO_INK);
    ico_rrect(tile, cx, cy - R - 10, 16, 9, 3, 0, ICO_INK, true);
    ico_line(tile, cx, cy - R - 5, cx, cy - R, 4, ICO_INK);
    ico_line(tile, cx + 21, cy - 21, cx + 28, cy - 28, 5, ICO_INK);
    ico_line(tile, cx, cy, cx + 15, cy - 15, 3, ICO_ACCENT);
    ico_dot(tile, cx, cy, 6, ICO_ACCENT);
}

static void draw_timer_icon(lv_obj_t *tile)
{
    const int cx = 90, cy = 78;
    const int top = cy - 30, bot = cy + 30, hw = 24;
    ico_line(tile, cx - hw, top, cx + hw, top, 4, ICO_INK);
    ico_line(tile, cx - hw, bot, cx + hw, bot, 4, ICO_INK);
    ico_line(tile, cx - hw + 2, top + 2, cx, cy, 3, ICO_INK);
    ico_line(tile, cx + hw - 2, top + 2, cx, cy, 3, ICO_INK);
    ico_line(tile, cx, cy, cx - hw + 2, bot - 2, 3, ICO_INK);
    ico_line(tile, cx, cy, cx + hw - 2, bot - 2, 3, ICO_INK);
    ico_line(tile, cx - 11, bot - 3, cx + 11, bot - 3, 4, ICO_ACCENT);
    ico_dot(tile, cx, cy + 6, 5, ICO_ACCENT);
}

static void draw_calendar_icon(lv_obj_t *tile)
{
    const int cx = 90, cy = 82, W = 70, H = 62;
    ico_rrect(tile, cx, cy, W, H, 12, 3, ICO_INK, false);
    ico_line(tile, cx - W / 2 + 6, cy - H / 2 + 16, cx + W / 2 - 6, cy - H / 2 + 16, 2, ICO_DIM);
    ico_line(tile, cx - 14, cy - H / 2 - 6, cx - 14, cy - H / 2 + 6, 4, ICO_INK);
    ico_line(tile, cx + 14, cy - H / 2 - 6, cx + 14, cy - H / 2 + 6, 4, ICO_INK);
    int gx[3] = { cx - 16, cx, cx + 16 };
    int gy[2] = { cy + 4, cy + 20 };
    for (int r = 0; r < 2; r++)
        for (int ci = 0; ci < 3; ci++)
            ico_dot(tile, gx[ci], gy[r], 7, (r == 0 && ci == 1) ? ICO_ACCENT : ICO_DIM);
}
void time_screen_create()
{
    time_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(time_screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(time_screen, 0, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(time_screen);
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_label_set_text(title, "TIME");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    // Two-column flex grid — same geometry as the Tools grid so the two
    // screens feel like siblings. With only four tiles the grid doesn't
    // need to scroll, but the dir is left vertical for future additions.
    lv_obj_t *grid = lv_obj_create(time_screen);
    lv_obj_set_size(grid, 400, 432);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_bg_color(grid, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(grid, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(grid, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(grid, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_column(grid, 12, LV_PART_MAIN);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid,
        LV_FLEX_ALIGN_SPACE_EVENLY,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START);

    // Insertion order (row-major, grid wraps every 2 tiles):
    //   [Alarm]   [Stopwatch]
    //   [Timer]   [Calendar]
    lv_obj_t *t_alarm     = make_tile(grid, "Alarm");
    lv_obj_t *t_stopwatch = make_tile(grid, "Stopwatch");
    lv_obj_t *t_timer     = make_tile(grid, "Timer");
    lv_obj_t *t_calendar  = make_tile(grid, "Calendar");

    draw_alarm_icon(t_alarm);
    draw_stopwatch_icon(t_stopwatch);
    draw_timer_icon(t_timer);
    draw_calendar_icon(t_calendar);

    lv_obj_add_event_cb(t_alarm,     [](lv_event_t *) { alarm_screen_show();    }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(t_stopwatch, [](lv_event_t *) { stopwatch_screen_show(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(t_timer,     [](lv_event_t *) { timer_screen_show();    }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(t_calendar,  [](lv_event_t *) { calendar_screen_show(); }, LV_EVENT_CLICKED, NULL);

    // lv_obj_create() children are CLICKABLE by default and would otherwise
    // swallow taps on the icon shapes. EVENT_BUBBLE on every child sends taps
    // up to the tile's CLICKED handler — same trick the Tools screen uses.
    uint32_t tile_count = lv_obj_get_child_count(grid);
    for (uint32_t i = 0; i < tile_count; i++) {
        lv_obj_t *tile = lv_obj_get_child(grid, i);
        uint32_t kid_count = lv_obj_get_child_count(tile);
        for (uint32_t j = 0; j < kid_count; j++) {
            lv_obj_add_flag(lv_obj_get_child(tile, j), LV_OBJ_FLAG_EVENT_BUBBLE);
        }
    }

    lv_obj_add_event_cb(time_screen, on_gesture, LV_EVENT_GESTURE, NULL);
}

void time_screen_show()       { lv_scr_load(time_screen); }
bool time_screen_is_active()  { return lv_screen_active() == time_screen; }


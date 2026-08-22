/*
 * Notifications screen — a scrollable list of mirrored phone notifications with
 * a connection-status header. Reads the store in notify_ble.cpp; refreshed by a
 * 1 Hz timer while visible. Reached from the Tools grid.
 */
#include "notify_screen.h"
#include "notify_ble.h"
#include <lvgl.h>
#include <LilyGoLib.h>

extern void tools_screen_show();

#define COL_BG      lv_color_hex(0x000000)
#define COL_CARD    lv_color_hex(0x141416)
#define COL_INK     lv_color_hex(0xECECF0)
#define COL_INK2    lv_color_hex(0x8A8A90)
#define COL_ACCENT  lv_color_hex(0xFF7B2E)
#define COL_GOOD    lv_color_hex(0x37C76F)

static lv_obj_t *notify_screen = nullptr;
static lv_obj_t *list = nullptr;
static lv_obj_t *status_dot = nullptr;
static lv_obj_t *status_lbl = nullptr;
static lv_obj_t *empty_lbl = nullptr;
static lv_timer_t *refr_timer = nullptr;
static int last_shown = -1;
static bool last_conn = false;

static void on_gesture(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_BOTTOM) tools_screen_show();
}

static void add_card(const notif_t *n)
{
    lv_obj_t *card = lv_obj_create(list);
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, COL_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_set_style_pad_row(card, 3, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *src = lv_label_create(card);
    lv_obj_set_style_text_font(src, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(src, COL_ACCENT, 0);
    lv_label_set_text(src, n->src[0] ? n->src : "Notification");

    if (n->title[0]) {
        lv_obj_t *t = lv_label_create(card);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(t, COL_INK, 0);
        lv_label_set_text(t, n->title);
        lv_obj_set_width(t, lv_pct(100));
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    }
    if (n->body[0]) {
        lv_obj_t *b = lv_label_create(card);
        lv_obj_set_style_text_font(b, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(b, COL_INK2, 0);
        lv_label_set_text(b, n->body);
        lv_obj_set_width(b, lv_pct(100));
        lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
    }
}

static void rebuild_list(void)
{
    lv_obj_clean(list);
    int c = notify_count();
    for (int i = 0; i < c; i++) {
        const notif_t *n = notify_get(i);
        if (n) add_card(n);
    }
    if (empty_lbl) lv_obj_add_flag(empty_lbl, LV_OBJ_FLAG_HIDDEN);
    if (c == 0 && empty_lbl) lv_obj_clear_flag(empty_lbl, LV_OBJ_FLAG_HIDDEN);
    last_shown = c;
}

static void update_status(void)
{
    bool conn = notify_ble_connected();
    lv_obj_set_style_bg_color(status_dot, conn ? COL_GOOD : COL_INK2, 0);
    lv_label_set_text(status_lbl, conn ? "Connected" : "Waiting for phone…");
    last_conn = conn;
}

static void on_refresh(lv_timer_t *)
{
    if (notify_count() != last_shown) rebuild_list();
    if (notify_ble_connected() != last_conn) update_status();
}

void notify_screen_create()
{
    notify_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(notify_screen, COL_BG, 0);
    lv_obj_set_style_bg_opa(notify_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(notify_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(notify_screen, on_gesture, LV_EVENT_GESTURE, NULL);

    /* header */
    lv_obj_t *hdr = lv_label_create(notify_screen);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(hdr, COL_INK, 0);
    lv_label_set_text(hdr, "Notifications");
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 16);

    status_dot = lv_obj_create(notify_screen);
    lv_obj_remove_style_all(status_dot);
    lv_obj_set_size(status_dot, 10, 10);
    lv_obj_set_style_radius(status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(status_dot, LV_OPA_COVER, 0);
    lv_obj_align(status_dot, LV_ALIGN_TOP_LEFT, 24, 52);
    status_lbl = lv_label_create(notify_screen);
    lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_lbl, COL_INK2, 0);
    lv_obj_align(status_lbl, LV_ALIGN_TOP_LEFT, 42, 49);

    /* scrollable list */
    list = lv_obj_create(notify_screen);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, lv_pct(92), 400);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 78);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    empty_lbl = lv_label_create(notify_screen);
    lv_obj_set_style_text_font(empty_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(empty_lbl, COL_INK2, 0);
    lv_label_set_text(empty_lbl, "No notifications yet.\nPair Gadgetbridge to your phone.");
    lv_obj_set_style_text_align(empty_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(empty_lbl, LV_ALIGN_CENTER, 0, 20);
}

void notify_screen_show()
{
    if (!notify_screen) notify_screen_create();
    update_status();
    rebuild_list();
    if (!refr_timer) refr_timer = lv_timer_create(on_refresh, 1000, NULL);
    lv_timer_resume(refr_timer);
    lv_scr_load(notify_screen);
}

bool notify_screen_is_active()
{
    return notify_screen && lv_scr_act() == notify_screen;
}

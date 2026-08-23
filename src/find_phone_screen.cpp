/*
 * Find Phone — rings the paired phone via Gadgetbridge's findPhone command.
 * Opening the screen starts the ring (if a phone is connected); Stop or swiping
 * down stops it and returns to Tools.
 */
#include "find_phone_screen.h"
#include "notify_ble.h"
#include <lvgl.h>

extern void tools_screen_show();

#define COL_BG      lv_color_hex(0x000000)
#define COL_CARD    lv_color_hex(0x141416)
#define COL_INK     lv_color_hex(0xECECF0)
#define COL_INK2    lv_color_hex(0x8A8A90)
#define COL_ACCENT  lv_color_hex(0xFF7B2E)
#define COL_GOOD    lv_color_hex(0x37C76F)

static lv_obj_t *screen = nullptr;
static lv_obj_t *status = nullptr;

static void stop_and_exit(void)
{
    notify_ble_find_phone(false);   /* safe no-op if never started / disconnected */
    tools_screen_show();
}

static void on_gesture(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_BOTTOM) stop_and_exit();
}

static void on_stop(lv_event_t *e) { stop_and_exit(); }

static void build(void)
{
    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, COL_BG, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screen, on_gesture, LV_EVENT_GESTURE, NULL);

    lv_obj_t *icon = lv_label_create(screen);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(icon, COL_ACCENT, 0);
    lv_label_set_text(icon, LV_SYMBOL_CALL);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 70);

    lv_obj_t *hdr = lv_label_create(screen);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(hdr, COL_INK, 0);
    lv_label_set_text(hdr, "Find Phone");
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 140);

    status = lv_label_create(screen);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(status, COL_INK2, 0);
    lv_obj_set_width(status, lv_pct(80));
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *stop = lv_button_create(screen);
    lv_obj_set_size(stop, 160, 56);
    lv_obj_align(stop, LV_ALIGN_BOTTOM_MID, 0, -70);
    lv_obj_set_style_bg_color(stop, COL_CARD, 0);
    lv_obj_set_style_radius(stop, 16, 0);
    lv_obj_set_style_shadow_width(stop, 0, 0);
    lv_obj_add_event_cb(stop, on_stop, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(stop);
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(sl, COL_ACCENT, 0);
    lv_label_set_text(sl, "Stop");
    lv_obj_center(sl);
}

void find_phone_screen_show()
{
    if (!screen) build();

    if (notify_ble_connected()) {
        notify_ble_find_phone(true);
        lv_label_set_text(status, "Ringing your phone\xE2\x80\xA6\n\nTap Stop when you've found it.");
        lv_obj_set_style_text_color(status, COL_GOOD, 0);
    } else {
        lv_label_set_text(status, "Phone not connected.\n\nConnect Gadgetbridge, then try again.");
        lv_obj_set_style_text_color(status, COL_INK2, 0);
    }
    lv_scr_load(screen);
}

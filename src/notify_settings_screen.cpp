/*
 * Notifications settings — vibrate, sound, and Do-Not-Disturb toggles for the
 * notification link. Reached from the gear icon on the Notify screen; swipe down
 * to go back. State is owned + persisted by notify_ble.cpp.
 */
#include "notify_settings_screen.h"
#include "notify_screen.h"
#include "notify_ble.h"
#include <lvgl.h>

#define COL_BG      lv_color_hex(0x000000)
#define COL_CARD    lv_color_hex(0x141416)
#define COL_INK     lv_color_hex(0xECECF0)
#define COL_INK2    lv_color_hex(0x8A8A90)
#define COL_ACCENT  lv_color_hex(0xFF7B2E)
#define COL_GOOD    lv_color_hex(0x37C76F)

static lv_obj_t *settings_screen = nullptr;
static lv_obj_t *vib_sw = nullptr, *snd_sw = nullptr, *dnd_sw = nullptr;

static void on_gesture(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_BOTTOM) notify_screen_show();
}

static void on_vib(lv_event_t *e) { notify_set_vibrate(lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED)); }
static void on_snd(lv_event_t *e) { notify_set_sound  (lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED)); }
static void on_dnd(lv_event_t *e) { notify_set_dnd    (lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED)); }

/* A titled row: description on the left, a switch on the right. */
static lv_obj_t *make_row(lv_obj_t *parent, const char *title, const char *sub,
                          bool on, lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row, COL_CARD, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_set_style_pad_hor(row, 16, 0);
    lv_obj_set_style_pad_ver(row, 12, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *txt = lv_obj_create(row);
    lv_obj_remove_style_all(txt);
    lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(txt, 2, 0);
    lv_obj_set_width(txt, lv_pct(66));
    lv_obj_align(txt, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_clear_flag(txt, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(txt);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(t, COL_INK, 0);
    lv_label_set_text(t, title);
    if (sub && sub[0]) {
        lv_obj_t *s = lv_label_create(txt);
        lv_obj_set_style_text_font(s, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s, COL_INK2, 0);
        lv_obj_set_width(s, lv_pct(100));
        lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
        lv_label_set_text(s, sub);
    }

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 74, 38);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x333338), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, COL_GOOD, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sw;
}

static void build(void)
{
    settings_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(settings_screen, COL_BG, 0);
    lv_obj_set_style_bg_opa(settings_screen, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(settings_screen, on_gesture, LV_EVENT_GESTURE, NULL);
    lv_obj_set_flex_flow(settings_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(settings_screen, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(settings_screen, 16, 0);
    lv_obj_set_style_pad_hor(settings_screen, 18, 0);
    lv_obj_set_style_pad_row(settings_screen, 12, 0);
    lv_obj_set_scroll_dir(settings_screen, LV_DIR_VER);

    lv_obj_t *hdr = lv_label_create(settings_screen);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(hdr, COL_INK, 0);
    lv_label_set_text(hdr, "Notify Settings");
    lv_obj_set_style_pad_bottom(hdr, 4, 0);

    vib_sw = make_row(settings_screen, "Vibrate", "Buzz on notifications and calls",
                      notify_get_vibrate(), on_vib);
    snd_sw = make_row(settings_screen, "Sound", "Play a ringtone on alerts",
                      notify_get_sound(), on_snd);
    dnd_sw = make_row(settings_screen, "Do Not Disturb", "Ignore all incoming notifications",
                      notify_get_dnd(), on_dnd);
}

void notify_settings_screen_show()
{
    if (!settings_screen) build();
    /* Reflect current state each time it opens (defaults may have loaded late). */
    if (notify_get_vibrate()) lv_obj_add_state(vib_sw, LV_STATE_CHECKED); else lv_obj_clear_state(vib_sw, LV_STATE_CHECKED);
    if (notify_get_sound())   lv_obj_add_state(snd_sw, LV_STATE_CHECKED); else lv_obj_clear_state(snd_sw, LV_STATE_CHECKED);
    if (notify_get_dnd())     lv_obj_add_state(dnd_sw, LV_STATE_CHECKED); else lv_obj_clear_state(dnd_sw, LV_STATE_CHECKED);
    lv_scr_load(settings_screen);
}

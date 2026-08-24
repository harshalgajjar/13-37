/*
 * Notifications screen — a scrollable list of mirrored phone notifications with
 * a connection-status header. Reads the store in notify_ble.cpp; refreshed by a
 * 1 Hz timer while visible. Reached from the Tools grid.
 */
#include "notify_screen.h"
#include "notify_settings_screen.h"
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
static lv_obj_t *pin_lbl = nullptr;
static lv_obj_t *diag_lbl = nullptr;   // power-opt verification readout
static lv_obj_t *empty_lbl = nullptr;

// Power-opt diagnostics, defined in main.cpp.
extern uint32_t main_cpu_mhz(void);
extern uint32_t main_loop_hz(void);
extern bool     clock_is_dimmed(void);
static lv_timer_t *refr_timer = nullptr;
static int last_shown = -1;
static bool last_conn = false;

static void on_gesture(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_BOTTOM) tools_screen_show();
}

static void rebuild_list(void);   /* fwd */

static void on_clear(lv_event_t *e)
{
    notify_clear_all();
    rebuild_list();
}

static void on_settings(lv_event_t *e)
{
    notify_settings_screen_show();
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
    if (!notify_ble_active()) {   /* a scan tool holds the BLE radio */
        lv_obj_set_style_bg_color(status_dot, COL_INK2, 0);
        lv_label_set_text(status_lbl, "Bluetooth busy \xE2\x80\x94 close scan tools");
        if (pin_lbl) lv_obj_add_flag(pin_lbl, LV_OBJ_FLAG_HIDDEN);
        last_conn = false;
        return;
    }
    bool conn = notify_ble_connected();
    /* Show the pairing PIN only while no phone is connected. */
    if (pin_lbl) {
        if (conn) lv_obj_add_flag(pin_lbl, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_clear_flag(pin_lbl, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_bg_color(status_dot, conn ? COL_GOOD : COL_INK2, 0);
    if (conn) lv_label_set_text(status_lbl, "Connected");
    else      lv_label_set_text_fmt(status_lbl, "Advertising  c:%lu d:%lu r:0x%02X",
                  (unsigned long)notify_ble_connects(),
                  (unsigned long)notify_ble_disconnects(),
                  notify_ble_last_reason());
    last_conn = conn;

    /* Power-opt verification: CPU freq (#6), live connection interval (#2), and
     * the main-loop rate (#5 — collapses when the screen dims). */
    if (diag_lbl) {
        lv_label_set_text_fmt(diag_lbl,
            "CPU %lu MHz    loop %lu/s%s\nBT link every %u ms",
            (unsigned long)main_cpu_mhz(),
            (unsigned long)main_loop_hz(),
            clock_is_dimmed() ? "  (dim)" : "",
            notify_ble_conn_interval_ms());
    }
}

static void on_refresh(lv_timer_t *)
{
    if (notify_count() != last_shown) rebuild_list();
    update_status();   /* live counts even when the connection state is unchanged */
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

    /* Clear-all button, top-right */
    lv_obj_t *clr = lv_button_create(notify_screen);
    lv_obj_set_size(clr, 66, 34);
    lv_obj_align(clr, LV_ALIGN_TOP_RIGHT, -12, 10);
    lv_obj_set_style_bg_color(clr, COL_CARD, 0);
    lv_obj_set_style_radius(clr, 10, 0);
    lv_obj_set_style_shadow_width(clr, 0, 0);
    lv_obj_add_event_cb(clr, on_clear, LV_EVENT_CLICKED, NULL);
    lv_obj_t *clrl = lv_label_create(clr);
    lv_obj_set_style_text_font(clrl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(clrl, COL_ACCENT, 0);
    lv_label_set_text(clrl, "Clear");
    lv_obj_center(clrl);

    /* Settings gear, top-left */
    lv_obj_t *gear = lv_button_create(notify_screen);
    lv_obj_set_size(gear, 40, 34);
    lv_obj_align(gear, LV_ALIGN_TOP_LEFT, 12, 10);
    lv_obj_set_style_bg_color(gear, COL_CARD, 0);
    lv_obj_set_style_radius(gear, 10, 0);
    lv_obj_set_style_shadow_width(gear, 0, 0);
    lv_obj_add_event_cb(gear, on_settings, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gicon = lv_label_create(gear);
    lv_obj_set_style_text_font(gicon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(gicon, COL_INK, 0);
    lv_label_set_text(gicon, LV_SYMBOL_SETTINGS);
    lv_obj_center(gicon);

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

    /* Pairing PIN — shown while unpaired so the user knows what to type on the
     * phone; hidden once a phone is connected. */
    pin_lbl = lv_label_create(notify_screen);
    lv_obj_set_style_text_font(pin_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(pin_lbl, COL_ACCENT, 0);
    lv_obj_align(pin_lbl, LV_ALIGN_TOP_MID, 0, 74);
    lv_label_set_text_fmt(pin_lbl, "Pairing PIN  %lu", (unsigned long)NOTIFY_PIN);

    /* Power-opt verification readout — kept large + centred + bright so it's easy
     * to read (it sits above the list, away from the rounded corners). */
    diag_lbl = lv_label_create(notify_screen);
    lv_obj_set_style_text_font(diag_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(diag_lbl, COL_INK, 0);
    lv_obj_set_style_text_align(diag_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(diag_lbl, lv_pct(96));
    lv_obj_align(diag_lbl, LV_ALIGN_BOTTOM_MID, 0, -32);

    /* scrollable list */
    list = lv_obj_create(notify_screen);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, lv_pct(92), 300);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 100);
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
    if (!notify_ble_active()) notify_ble_begin();   /* start advertising on-demand */
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

/* ---- popup banner over any screen ---- */

#define COL_REJECT  lv_color_hex(0xE5484D)
#define COL_BTN     lv_color_hex(0x333338)

static lv_obj_t *banner = nullptr;
static bool      banner_is_call = false;   /* current banner is the call banner */
static uint32_t  s_last_total = 0;

static void banner_del(void)
{
    if (banner) { lv_obj_del(banner); banner = nullptr; }
    banner_is_call = false;
}

static void banner_dismiss(lv_timer_t *t) { banner_del(); lv_timer_del(t); }
static void banner_clicked(lv_event_t *e) { banner_del(); notify_screen_show(); }

/* Build the shared card (attached to the active screen — not lv_layer_top, whose
 * pixels get erased by this firmware's per-second partial clock repaint) with the
 * source / title / body labels. Returns the card; caller adds a timer or buttons. */
static lv_obj_t *build_banner_card(const notif_t *n)
{
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, lv_pct(94));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_bg_color(card, COL_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 2, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, COL_ACCENT, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *src = lv_label_create(card);
    lv_obj_set_style_text_font(src, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(src, COL_ACCENT, 0);
    lv_label_set_text(src, n->src[0] ? n->src : "Notification");

    if (n->title[0]) {
        lv_obj_t *t = lv_label_create(card);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(t, COL_INK, 0);
        lv_obj_set_width(t, lv_pct(100));
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_label_set_text(t, n->title);
    }
    if (n->body[0]) {
        lv_obj_t *b = lv_label_create(card);
        lv_obj_set_style_text_font(b, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(b, COL_INK2, 0);
        lv_obj_set_width(b, lv_pct(100));
        lv_label_set_long_mode(b, LV_LABEL_LONG_DOT);
        lv_label_set_text(b, n->body);
    }
    return card;
}

static void show_banner(const notif_t *n)
{
    banner_del();
    banner = build_banner_card(n);
    banner_is_call = false;
    lv_obj_add_flag(banner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(banner, banner_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_move_foreground(banner);
    lv_timer_create(banner_dismiss, 6000, NULL);    /* auto-dismiss after 6 s */
}

/* Button callbacks just signal the BLE layer; the call banner is torn down by
 * notify_ui_poll once the ring stops, so we never delete it mid-event. */
static void call_silence_cb(lv_event_t *e) { notify_ble_call_silence(); }
static void call_reject_cb (lv_event_t *e) { notify_ble_call_reject();  }

static lv_obj_t *make_call_btn(lv_obj_t *row, const char *txt, lv_color_t bg,
                               lv_color_t fg, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(row);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_height(btn, 46);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(btn);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, fg, 0);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    return btn;
}

static void show_call_banner(const notif_t *n)
{
    banner_del();
    banner = build_banner_card(n);
    banner_is_call = true;
    lv_obj_set_style_border_color(banner, COL_GOOD, 0);   /* distinct from notifications */

    lv_obj_t *row = lv_obj_create(banner);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(row, 8, 0);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    make_call_btn(row, "Silence", COL_BTN,    COL_INK,          call_silence_cb);
    make_call_btn(row, "Reject",  COL_REJECT, lv_color_white(), call_reject_cb);

    lv_obj_move_foreground(banner);
    /* No auto-dismiss: notify_ui_poll removes it when the ring stops. */
}

void notify_ui_poll(void)
{
    /* Incoming-call banner: show it while ringing, and tear it down the instant
     * the ring stops — whether by Reject, Silence, the phone ending the call, or
     * the ring timeout. */
    if (notify_ble_call_ringing()) {
        if (!banner_is_call) {
            const notif_t *n = notify_get(0);   /* the call entry is newest */
            if (n) show_call_banner(n);
            /* Don't let the call's store entry re-pop as a notification banner
               after the call clears. */
            s_last_total = notify_ble_total_added();
        }
        return;
    }
    if (banner_is_call) banner_del();

    uint32_t t = notify_ble_total_added();
    if (t == s_last_total) return;
    s_last_total = t;
    /* Don't stack a banner on top of the list screen — it's already visible there. */
    if (notify_screen_is_active()) return;
    if (banner) return;   /* one banner at a time */
    const notif_t *n = notify_get(0);   /* newest */
    if (n) show_banner(n);
}

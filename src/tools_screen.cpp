#include "tools_screen.h"
#include "notify_screen.h"
#include "find_phone_screen.h"
#include "airtag.h"
#include "flipper.h"
#include "skimmer.h"
#include "evil_twin.h"
#include "flock.h"
#include "tesla_cp_screen.h"
#include "tpms_screen.h"
#include "pager_screen.h"
#include "mouse_screen.h"
#include "usb_sd_screen.h"
#include "aprs_screen.h"
#include "wifi_screen.h"
#include "analyze_screen.h"
#include "bt_analyze_screen.h"
#include "notify_ble.h"   // notify_ble_active / notify_ble_stop — radio arbitration
#include "mouse_hid.h"    // mouse_hid_is_running / mouse_hid_stop
#include <LilyGoLib.h>

// Defined in main.cpp
void clock_screen_show();
void main_loop_request_lvgl_priority(int cycles);

// ---- Radio-conflict guard -----------------------------------------------
// The notification link and the BLE mouse own the radio via BLEDevice; the
// scanner/analyzer tools own it via ble_scan_manager (BLE) or the WiFi driver.
// This build can't run WiFi or a BLE scanner alongside the BLE link — starting
// one while the link is up freezes the watch. So if a link owner is active,
// confirm with the user first, then release it before running the tool (the
// notify keep-alive rebuilds the link automatically once the tool stops).
typedef void (*radio_action_fn)(void);
static radio_action_fn s_pending_action = nullptr;
static lv_obj_t       *s_conflict_modal = nullptr;

static bool ble_link_owner_active()
{
    return notify_ble_active() || mouse_hid_is_running();
}

static void run_pending_action()
{
    notify_ble_stop();                              // release the radio
    if (mouse_hid_is_running()) mouse_hid_stop();
    radio_action_fn a = s_pending_action;
    s_pending_action = nullptr;
    if (a) a();
}

static void conflict_close()
{
    if (s_conflict_modal) { lv_obj_del(s_conflict_modal); s_conflict_modal = nullptr; }
}
static void conflict_continue_cb(lv_event_t *) { conflict_close(); run_pending_action(); }
static void conflict_cancel_cb(lv_event_t *)   { conflict_close(); s_pending_action = nullptr; }

static void show_conflict_modal()
{
    s_conflict_modal = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_conflict_modal);
    lv_obj_set_size(s_conflict_modal, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_conflict_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_conflict_modal, LV_OPA_70, 0);
    lv_obj_clear_flag(s_conflict_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(s_conflict_modal);
    lv_obj_set_width(card, lv_pct(82));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x141416), 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_pad_row(card, 16, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *txt = lv_label_create(card);
    lv_obj_set_width(txt, lv_pct(100));
    lv_label_set_long_mode(txt, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(txt, lv_color_hex(0xECECF0), 0);
    lv_obj_set_style_text_font(txt, &lv_font_montserrat_16, 0);
    lv_label_set_text(txt, "This scanner needs Bluetooth and will disconnect phone "
                           "notifications until you stop it. Continue?");

    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cancel = lv_button_create(row);
    lv_obj_set_flex_grow(cancel, 1);
    lv_obj_set_height(cancel, 48);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(0x333338), 0);
    lv_obj_set_style_shadow_width(cancel, 0, 0);
    lv_obj_add_event_cb(cancel, conflict_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(cancel);
    lv_obj_set_style_text_color(cl, lv_color_hex(0xECECF0), 0);
    lv_label_set_text(cl, "Cancel");
    lv_obj_center(cl);

    lv_obj_t *cont = lv_button_create(row);
    lv_obj_set_flex_grow(cont, 1);
    lv_obj_set_height(cont, 48);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0xFF7B2E), 0);
    lv_obj_set_style_shadow_width(cont, 0, 0);
    lv_obj_add_event_cb(cont, conflict_continue_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ct = lv_label_create(cont);
    lv_obj_set_style_text_color(ct, lv_color_white(), 0);
    lv_label_set_text(ct, "Continue");
    lv_obj_center(ct);
}

// Run a radio tool, first confirming + releasing the BLE link if it's in use.
static void guard_radio(radio_action_fn action)
{
    if (ble_link_owner_active()) {
        s_pending_action = action;
        show_conflict_modal();
    } else {
        action();
    }
}

// ---- Analyze band chooser -----------------------------------------------
// "Analyze" is really three analyzers chained by swipes (WiFi -> Bluetooth ->
// LoRa), which is confusing on entry. Ask which band up front. WiFi and BT both
// need the radio, so picking either also releases the notification link.
static void analyze_release_and(radio_action_fn open)
{
    conflict_close();
    notify_ble_stop();
    if (mouse_hid_is_running()) mouse_hid_stop();
    open();
}
static void analyze_pick_wifi(lv_event_t *) { analyze_release_and(analyze_screen_show); }
static void analyze_pick_bt(lv_event_t *)   { analyze_release_and(bt_analyze_screen_show); }

static void show_analyze_chooser()
{
    s_conflict_modal = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_conflict_modal);
    lv_obj_set_size(s_conflict_modal, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_conflict_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_conflict_modal, LV_OPA_70, 0);
    lv_obj_clear_flag(s_conflict_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(s_conflict_modal);
    lv_obj_set_width(card, lv_pct(82));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x141416), 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *txt = lv_label_create(card);
    lv_obj_set_width(txt, lv_pct(100));
    lv_label_set_long_mode(txt, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(txt, lv_color_hex(0xECECF0), 0);
    lv_obj_set_style_text_font(txt, &lv_font_montserrat_16, 0);
    lv_label_set_text(txt, "Analyze which band?\nScanning pauses notifications.");

    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *wifi = lv_button_create(row);
    lv_obj_set_flex_grow(wifi, 1);
    lv_obj_set_height(wifi, 48);
    lv_obj_set_style_bg_color(wifi, lv_color_hex(0x2E6BFF), 0);
    lv_obj_set_style_shadow_width(wifi, 0, 0);
    lv_obj_add_event_cb(wifi, analyze_pick_wifi, LV_EVENT_CLICKED, NULL);
    lv_obj_t *wl = lv_label_create(wifi);
    lv_obj_set_style_text_color(wl, lv_color_white(), 0);
    lv_label_set_text(wl, "WiFi");
    lv_obj_center(wl);

    lv_obj_t *bt = lv_button_create(row);
    lv_obj_set_flex_grow(bt, 1);
    lv_obj_set_height(bt, 48);
    lv_obj_set_style_bg_color(bt, lv_color_hex(0x00A0C0), 0);
    lv_obj_set_style_shadow_width(bt, 0, 0);
    lv_obj_add_event_cb(bt, analyze_pick_bt, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(bt);
    lv_obj_set_style_text_color(bl, lv_color_white(), 0);
    lv_label_set_text(bl, "Bluetooth");
    lv_obj_center(bl);

    lv_obj_t *cancel = lv_button_create(card);
    lv_obj_set_width(cancel, lv_pct(100));
    lv_obj_set_height(cancel, 44);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(0x333338), 0);
    lv_obj_set_style_shadow_width(cancel, 0, 0);
    lv_obj_add_event_cb(cancel, conflict_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(cancel);
    lv_obj_set_style_text_color(cl, lv_color_hex(0xECECF0), 0);
    lv_label_set_text(cl, "Cancel");
    lv_obj_center(cl);
}

static lv_obj_t *tools_screen;
static lv_obj_t *t_airtag;    // referenced by on_airtag_clicked for colour swap
static lv_obj_t *t_flipper;   // referenced by on_flipper_clicked for colour swap
static lv_obj_t *t_skimmer;   // referenced by on_skimmer_clicked for colour swap
static lv_obj_t *t_eviltwin;  // referenced by on_eviltwin_clicked for colour swap
static lv_obj_t *t_flock;     // referenced by on_flock_clicked for colour swap

static void on_gesture(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_TOP)
        clock_screen_show();
}

static void set_airtag_tile_running(bool running)
{
    lv_obj_set_style_bg_color(t_airtag,
        running ? lv_color_make(0x00, 0x55, 0x22)
                : lv_color_make(0x11, 0x11, 0x11),
        LV_PART_MAIN);
}

static void on_airtag_clicked(lv_event_t *e)
{
    if (airtag_is_running()) {
        airtag_stop();
        set_airtag_tile_running(false);
    } else {
        guard_radio([]{ set_airtag_tile_running(airtag_start()); });
    }
}

static void set_flipper_tile_running(bool running)
{
    lv_obj_set_style_bg_color(t_flipper,
        running ? lv_color_make(0x00, 0x55, 0x22)
                : lv_color_make(0x11, 0x11, 0x11),
        LV_PART_MAIN);
}

static void on_flipper_clicked(lv_event_t *e)
{
    if (flipper_is_running()) {
        flipper_stop();
        set_flipper_tile_running(false);
    } else {
        guard_radio([]{ set_flipper_tile_running(flipper_start()); });
    }
}

static void set_skimmer_tile_running(bool running)
{
    lv_obj_set_style_bg_color(t_skimmer,
        running ? lv_color_make(0x00, 0x55, 0x22)
                : lv_color_make(0x11, 0x11, 0x11),
        LV_PART_MAIN);
}

static void on_skimmer_clicked(lv_event_t *e)
{
    if (skimmer_is_running()) {
        skimmer_stop();
        set_skimmer_tile_running(false);
    } else {
        guard_radio([]{ set_skimmer_tile_running(skimmer_start()); });
    }
}

static void set_eviltwin_tile_running(bool running)
{
    lv_obj_set_style_bg_color(t_eviltwin,
        running ? lv_color_make(0x00, 0x55, 0x22)
                : lv_color_make(0x11, 0x11, 0x11),
        LV_PART_MAIN);
}

static void on_eviltwin_clicked(lv_event_t *e)
{
    if (evil_twin_is_running()) {
        evil_twin_stop();
        set_eviltwin_tile_running(false);
    } else {
        guard_radio([]{ set_eviltwin_tile_running(evil_twin_start()); });  // WiFi
    }
}

static void set_flock_tile_running(bool running)
{
    lv_obj_set_style_bg_color(t_flock,
        running ? lv_color_make(0x00, 0x55, 0x22)
                : lv_color_make(0x11, 0x11, 0x11),
        LV_PART_MAIN);
}

static void on_flock_clicked(lv_event_t *e)
{
    if (flock_is_running()) {
        flock_stop();
        set_flock_tile_running(false);
    } else {
        guard_radio([]{ set_flock_tile_running(flock_start()); });
    }
}

// Tile container — 180x180 button-like card with a label at the bottom.
// The icon-drawing helpers below fill the upper portion using LVGL primitives
// (no image assets needed). The tile is clickable so future feature wiring
// is a single lv_obj_add_event_cb call per tile.
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


// ---- Polished line-art icons ----------------------------------------------
#define ICO_INK     lv_color_hex(0xE8E8EA)
#define ICO_DIM     lv_color_hex(0x6E6E73)
#define ICO_ACCENT  lv_color_hex(0xFF7B2E)

static void ico_ring(lv_obj_t *p, int cx, int cy, int d, int w, lv_color_t c){
    lv_obj_t *o=lv_obj_create(p); lv_obj_remove_style_all(o);
    lv_obj_set_size(o,d,d); lv_obj_set_pos(o,cx-d/2,cy-d/2);
    lv_obj_set_style_radius(o,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_border_width(o,w,0); lv_obj_set_style_border_color(o,c,0);
    lv_obj_set_style_bg_opa(o,LV_OPA_TRANSP,0); lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);
}
static void ico_line(lv_obj_t *p,int x1,int y1,int x2,int y2,int w,lv_color_t c){
    lv_point_precise_t *pts=(lv_point_precise_t*)lv_malloc(sizeof(lv_point_precise_t)*2);
    pts[0].x=x1;pts[0].y=y1;pts[1].x=x2;pts[1].y=y2;
    lv_obj_t *l=lv_line_create(p); lv_line_set_points(l,pts,2);
    lv_obj_set_style_line_width(l,w,0); lv_obj_set_style_line_color(l,c,0);
    lv_obj_set_style_line_rounded(l,true,0);
}
static void ico_dot(lv_obj_t *p,int cx,int cy,int d,lv_color_t c){
    lv_obj_t *o=lv_obj_create(p); lv_obj_remove_style_all(o);
    lv_obj_set_size(o,d,d); lv_obj_set_pos(o,cx-d/2,cy-d/2);
    lv_obj_set_style_radius(o,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(o,c,0); lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);
}
static void ico_rrect(lv_obj_t *p,int cx,int cy,int w,int h,int r,int sw,lv_color_t c,bool fill){
    lv_obj_t *o=lv_obj_create(p); lv_obj_remove_style_all(o);
    lv_obj_set_size(o,w,h); lv_obj_set_pos(o,cx-w/2,cy-h/2); lv_obj_set_style_radius(o,r,0);
    if(fill){ lv_obj_set_style_bg_color(o,c,0); lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0); }
    else{ lv_obj_set_style_bg_opa(o,LV_OPA_TRANSP,0); lv_obj_set_style_border_width(o,sw,0); lv_obj_set_style_border_color(o,c,0); }
    lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);
}
static void ico_arc(lv_obj_t *p,int cx,int cy,int d,int start,int end,int w,lv_color_t c){
    lv_obj_t *a=lv_arc_create(p); lv_obj_remove_style_all(a);
    lv_obj_set_size(a,d,d); lv_obj_set_pos(a,cx-d/2,cy-d/2);
    lv_arc_set_bg_angles(a,start,end);
    lv_obj_set_style_arc_width(a,w,LV_PART_MAIN); lv_obj_set_style_arc_color(a,c,LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a,true,LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a,LV_OPA_TRANSP,LV_PART_INDICATOR);
    lv_obj_remove_flag(a,LV_OBJ_FLAG_CLICKABLE);
}

static void draw_wifi_icon(lv_obj_t *t){
    const int cx=90, cy=104;
    ico_arc(t,cx,cy,82,220,320,4,ICO_INK); ico_arc(t,cx,cy,58,220,320,4,ICO_INK);
    ico_arc(t,cx,cy,34,220,320,4,ICO_INK); ico_dot(t,cx,cy-2,9,ICO_ACCENT);
}
static void draw_analyzer_icon(lv_obj_t *t){
    int x[5]={64,77,90,103,116}, h[5]={26,46,34,58,30}; lv_color_t c[5]={ICO_DIM,ICO_INK,ICO_DIM,ICO_ACCENT,ICO_DIM};
    for(int i=0;i<5;i++) ico_rrect(t,x[i],108-h[i]/2,9,h[i],4,0,c[i],true);
}
static void draw_airtag_icon(lv_obj_t *t){
    const int cx=86, cy=80;
    ico_ring(t,cx,cy,52,3,ICO_INK); ico_dot(t,cx,cy,8,ICO_INK);
    ico_arc(t,cx,cy,78,300,350,3,ICO_ACCENT); ico_arc(t,cx,cy,96,300,350,3,ICO_ACCENT);
}
static void draw_flipper_icon(lv_obj_t *t){
    const int cx=90, cy=76;
    ico_rrect(t,cx,cy,56,86,12,3,ICO_INK,false); ico_rrect(t,cx,cy-24,40,22,4,0,ICO_DIM,true);
    ico_ring(t,cx,cy+22,26,3,ICO_INK); ico_dot(t,cx,cy+22,6,ICO_ACCENT);
}
static void draw_skimmer_icon(lv_obj_t *t){
    const int cx=90, cy=70;
    ico_rrect(t,cx,cy,72,46,8,3,ICO_INK,false); ico_line(t,cx-36,cy-6,cx+36,cy-6,5,ICO_DIM);
    ico_line(t,cx-30,cy+38,cx+30,cy+38,6,ICO_ACCENT);
}
static void draw_eviltwin_icon(lv_obj_t *t){
    ico_arc(t,66,92,40,220,320,3,ICO_INK); ico_arc(t,66,92,22,220,320,3,ICO_INK); ico_dot(t,66,90,7,ICO_INK);
    ico_arc(t,116,92,40,220,320,3,ICO_ACCENT); ico_arc(t,116,92,22,220,320,3,ICO_ACCENT); ico_dot(t,116,90,7,ICO_ACCENT);
}
static void draw_flock_icon(lv_obj_t *t){
    const int cx=88, cy=72;
    ico_rrect(t,cx,cy,54,30,6,3,ICO_INK,false); ico_dot(t,cx+14,cy,9,ICO_ACCENT);
    ico_line(t,cx-22,cy-15,cx-30,cy-26,3,ICO_INK); ico_line(t,cx,cy+15,cx,cy+30,3,ICO_INK);
}
static void draw_microsd_icon(lv_obj_t *t){
    const int cx=90, cy=78, W=54, H=72;
    ico_rrect(t,cx,cy,W,H,8,3,ICO_INK,false); ico_line(t,cx-W/2,cy-H/2+16,cx-W/2+14,cy-H/2,3,ICO_INK);
    for(int i=0;i<4;i++) ico_line(t,cx-16+i*11,cy-H/2+8,cx-16+i*11,cy-H/2+22,3,(i==1)?ICO_ACCENT:ICO_DIM);
}
static void draw_pager_icon(lv_obj_t *t){
    const int cx=90, cy=78;
    ico_rrect(t,cx,cy,78,56,10,3,ICO_INK,false); ico_rrect(t,cx,cy-8,58,22,4,0,ICO_DIM,true);
    ico_dot(t,cx-16,cy+18,7,ICO_ACCENT); ico_dot(t,cx,cy+18,6,ICO_INK); ico_dot(t,cx+16,cy+18,6,ICO_INK);
}
static void draw_tpms_icon(lv_obj_t *t){
    const int cx=90, cy=74;
    ico_ring(t,cx,cy,68,7,ICO_INK); ico_ring(t,cx,cy,34,3,ICO_DIM); ico_line(t,cx,cy+34,cx,cy+44,5,ICO_ACCENT);
}
static void draw_mouse_icon(lv_obj_t *t){
    const int cx=90, cy=76;
    ico_rrect(t,cx,cy,50,78,24,3,ICO_INK,false); ico_line(t,cx,cy-39,cx,cy-8,2,ICO_INK);
    ico_rrect(t,cx,cy-26,5,12,3,0,ICO_ACCENT,true);
}
static void draw_aprs_icon(lv_obj_t *t){
    const int cx=90, ty=58, by=106;
    ico_line(t,cx,ty,cx,by,3,ICO_INK); ico_line(t,cx,by,cx-14,by+6,3,ICO_INK); ico_line(t,cx,by,cx+14,by+6,3,ICO_INK);
    ico_arc(t,cx,ty,34,210,330,3,ICO_ACCENT); ico_arc(t,cx,ty,56,210,330,3,ICO_ACCENT);
}
static void draw_tesla_cp_icon(lv_obj_t *t){
    const int cx=90, cy=76;
    ico_ring(t,cx,cy,62,3,ICO_INK);
    ico_line(t,cx+6,cy-22,cx-10,cy+2,4,ICO_ACCENT); ico_line(t,cx-10,cy+2,cx+2,cy+2,4,ICO_ACCENT);
    ico_line(t,cx+2,cy+2,cx-6,cy+24,4,ICO_ACCENT);
}
static void draw_notify_icon(lv_obj_t *t){
    /* clean bell glyph for now; can go custom line-art later */
    lv_obj_t *l = lv_label_create(t);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(l, ICO_INK, 0);
    lv_label_set_text(l, LV_SYMBOL_BELL);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 34);
}
static void draw_findphone_icon(lv_obj_t *t){
    lv_obj_t *l = lv_label_create(t);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(l, ICO_INK, 0);
    lv_label_set_text(l, LV_SYMBOL_CALL);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 34);
}

void tools_screen_create()
{
    tools_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(tools_screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(tools_screen, 0, LV_PART_MAIN);

    // Title
    lv_obj_t *title = lv_label_create(tools_screen);
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_label_set_text(title, "TOOLS");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    // Two-column flex grid. ROW_WRAP gives us 2 tiles per row (since each
    // 180px tile + the 12px column gap exceeds half the 384px inner width),
    // and the container scrolls vertically when future tiles overflow.
    lv_obj_t *grid = lv_obj_create(tools_screen);
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

    // Insertion order maps to row-major (grid wraps every 2 tiles):
    //   [WiFi]      [Analyze]
    //   [Mouse]     [USB SD]
    //   [Pager]     [TPMS]
    //   [LoRa APRS] [Tesla CP]
    //   [AirTag]    [Flipper]
    //   [Skimmers]  [Evil Twin]
    //   [Flock]
    // The timepiece tiles (Alarm / Stopwatch / Timer / Calendar) used to live
    // at the bottom of this grid; they moved to the TIME screen (swipe up
    // from the clock face).
    lv_obj_t *t_notify  = make_tile(grid, "Notify");
    lv_obj_t *t_findph  = make_tile(grid, "Find Phone");
    lv_obj_t *t_wifi    = make_tile(grid, "WiFi");
    lv_obj_t *t_analyze = make_tile(grid, "Analyze");
    lv_obj_t *t_mouse   = make_tile(grid, "Mouse");
    lv_obj_t *t_usbsd   = make_tile(grid, "USB SD");
    lv_obj_t *t_pager   = make_tile(grid, "Pager");
    lv_obj_t *t_tpms    = make_tile(grid, "TPMS");
    lv_obj_t *t_aprs    = make_tile(grid, "LoRa APRS");
    lv_obj_t *t_tesla   = make_tile(grid, "Tesla CP");
    t_airtag            = make_tile(grid, "AirTag");
    t_flipper           = make_tile(grid, "Flipper");
    t_skimmer           = make_tile(grid, "Skimmers");
    t_eviltwin          = make_tile(grid, "Evil Twin");
    t_flock             = make_tile(grid, "Flock");

    draw_notify_icon(t_notify);
    draw_findphone_icon(t_findph);
    draw_wifi_icon(t_wifi);
    draw_analyzer_icon(t_analyze);
    draw_mouse_icon(t_mouse);
    draw_microsd_icon(t_usbsd);
    draw_pager_icon(t_pager);
    draw_tpms_icon(t_tpms);
    draw_aprs_icon(t_aprs);
    draw_tesla_cp_icon(t_tesla);
    draw_airtag_icon(t_airtag);
    draw_flipper_icon(t_flipper);
    draw_skimmer_icon(t_skimmer);
    draw_eviltwin_icon(t_eviltwin);
    draw_flock_icon(t_flock);

    // Notify tile opens the mirrored-notifications screen.
    lv_obj_add_event_cb(t_notify, [](lv_event_t *) { notify_screen_show(); }, LV_EVENT_CLICKED, NULL);

    // Find Phone tile rings the paired phone via Gadgetbridge.
    lv_obj_add_event_cb(t_findph, [](lv_event_t *) { find_phone_screen_show(); }, LV_EVENT_CLICKED, NULL);

    // Tesla CP tile opens the 315 MHz charge-port-open transmit screen.
    lv_obj_add_event_cb(t_tesla, [](lv_event_t *) { tesla_cp_screen_show(); }, LV_EVENT_CLICKED, NULL);

    // AirTag tile toggles the BLE Find My sniffer and swaps to a dim green
    // background while running.
    lv_obj_add_event_cb(t_airtag, on_airtag_clicked, LV_EVENT_CLICKED, NULL);
    set_airtag_tile_running(airtag_is_running());

    // Flipper tile toggles the BLE Flipper Zero detector. Same dim-green
    // running indication as AirTag.
    lv_obj_add_event_cb(t_flipper, on_flipper_clicked, LV_EVENT_CLICKED, NULL);
    set_flipper_tile_running(flipper_is_running());

    // Skimmers tile toggles the HC-0x card-skimmer detector. Same green-
    // when-running affordance as AirTag and Flipper.
    lv_obj_add_event_cb(t_skimmer, on_skimmer_clicked, LV_EVENT_CLICKED, NULL);
    set_skimmer_tile_running(skimmer_is_running());

    // Evil Twin tile toggles the rogue-AP detector (WiFi beacon scan).
    lv_obj_add_event_cb(t_eviltwin, on_eviltwin_clicked, LV_EVENT_CLICKED, NULL);
    set_eviltwin_tile_running(evil_twin_is_running());

    // Flock tile toggles the surveillance-vendor detector (WiFi + BLE scan).
    lv_obj_add_event_cb(t_flock, on_flock_clicked, LV_EVENT_CLICKED, NULL);
    set_flock_tile_running(flock_is_running());

    // TPMS tile opens the TPMS monitor screen.
    lv_obj_add_event_cb(t_tpms, [](lv_event_t *) { tpms_screen_show(); }, LV_EVENT_CLICKED, NULL);

    // Pager tile opens the POCSAG/FLEX decoder screen.
    lv_obj_add_event_cb(t_pager, [](lv_event_t *) { pager_screen_show(); }, LV_EVENT_CLICKED, NULL);

    // Mouse tile opens the Bluetooth HID mouse screen.
    lv_obj_add_event_cb(t_mouse, [](lv_event_t *) { mouse_screen_show(); }, LV_EVENT_CLICKED, NULL);

    // USB SD tile opens the USB mass-storage card-reader screen.
    lv_obj_add_event_cb(t_usbsd, [](lv_event_t *) { usb_sd_screen_show(); }, LV_EVENT_CLICKED, NULL);

    // APRS tile opens the LoRa APRS receive/transmit screen.
    lv_obj_add_event_cb(t_aprs, [](lv_event_t *) { aprs_screen_show(); }, LV_EVENT_CLICKED, NULL);

    // WiFi tile opens the site-survey + ping-sweep screen.
    lv_obj_add_event_cb(t_wifi, [](lv_event_t *) { guard_radio(wifi_screen_show); }, LV_EVENT_CLICKED, NULL);

    // Analyze tile opens the WiFi channel utilisation visualisation.
    lv_obj_add_event_cb(t_analyze, [](lv_event_t *) { show_analyze_chooser(); }, LV_EVENT_CLICKED, NULL);

    // lv_obj_create() creates objects with LV_OBJ_FLAG_CLICKABLE set by
    // default, so the icon shapes inside each tile would otherwise swallow
    // CLICKED events instead of letting them reach the tile. Walk every tile
    // and add LV_OBJ_FLAG_EVENT_BUBBLE to each of its children so a tap
    // anywhere inside the tile (icon shapes, label, or background) reaches
    // the tile's CLICKED handler.
    uint32_t tile_count = lv_obj_get_child_count(grid);
    for (uint32_t i = 0; i < tile_count; i++) {
        lv_obj_t *tile = lv_obj_get_child(grid, i);
        uint32_t kid_count = lv_obj_get_child_count(tile);
        for (uint32_t j = 0; j < kid_count; j++) {
            lv_obj_add_flag(lv_obj_get_child(tile, j), LV_OBJ_FLAG_EVENT_BUBBLE);
        }
    }

    lv_obj_add_event_cb(tools_screen, on_gesture, LV_EVENT_GESTURE, NULL);
}

void tools_screen_show()
{
    main_loop_request_lvgl_priority(12);
    lv_scr_load(tools_screen);
}
bool tools_screen_is_active() { return lv_screen_active() == tools_screen; }


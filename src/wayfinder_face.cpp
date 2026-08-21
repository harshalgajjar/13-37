/*
 * Wayfinder face — see wayfinder_face.h.
 * Ported from the standalone simulator-verified face; adapted for C++ / LVGL 9.5
 * and 13:37's externally-driven clock. Reuses 13:37's 96px clock font.
 */
#include "wayfinder_face.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* 13:37 ships this big font (lv_font_montserrat_clock_96.c) */
extern "C" const lv_font_t lv_font_montserrat_clock_96;

#define SCR_W   410
#define SCR_H   502
#define CX      205
#define CY      250
#define R_OUT   168
#define R_IN    (R_OUT - 46)

#define COL_ACCENT    lv_color_hex(0xff7b2e)
#define COL_ACCENT_R  lv_color_hex(0xff3535)
#define COL_WHITE     lv_color_hex(0xffffff)
#define COL_INK2      lv_color_hex(0x9a9aa2)
#define COL_RED_INK   lv_color_hex(0xff6a6a)

typedef struct {
    lv_obj_t *root;
    lv_obj_t *ring;
    lv_obj_t *time_lbl;
    lv_obj_t *sec_lbl;
    lv_obj_t *bearing_lbl;
    lv_obj_t *date_lbl;
    lv_obj_t *card[4];
    lv_obj_t *comp_lbl[4];
    lv_obj_t *comp_val[4];
    struct tm now;
    bool built;
    bool dim;
    bool night;
} wf_t;

static wf_t S;

static lv_color_t wf_accent(void) { return S.night ? COL_ACCENT_R : COL_ACCENT; }
static lv_color_t wf_ink(void)    { return S.night ? COL_RED_INK  : COL_WHITE;  }

/* Parametrised compass so the full face and the preview share one draw path. */
static void draw_compass(lv_layer_t *layer, int cx, int cy, int r_out,
                         bool dim, bool night, bool show_sweep, int sec)
{
    lv_color_t acc = night ? COL_ACCENT_R : COL_ACCENT;
    int r_in = r_out - (r_out * 46 / 168);

    for (int i = 0; i < 60; i++) {
        double a = (i * 6 - 90) * M_PI / 180.0;
        bool major = (i % 5 == 0);
        int r1 = r_out;
        int r2 = r_out - (major ? (r_out * 18 / 168) : (r_out * 9 / 168));
        lv_draw_line_dsc_t d;
        lv_draw_line_dsc_init(&d);
        d.color = night ? COL_RED_INK : COL_WHITE;
        d.width = major ? (r_out > 90 ? 3 : 2) : 1;
        d.round_start = d.round_end = 1;
        if (dim) d.opa = major ? LV_OPA_50 : LV_OPA_10;
        else     d.opa = major ? LV_OPA_90 : LV_OPA_30;
        d.p1.x = cx + (lv_value_precise_t)(cos(a) * r1);
        d.p1.y = cy + (lv_value_precise_t)(sin(a) * r1);
        d.p2.x = cx + (lv_value_precise_t)(cos(a) * r2);
        d.p2.y = cy + (lv_value_precise_t)(sin(a) * r2);
        lv_draw_line(layer, &d);
    }

    lv_draw_triangle_dsc_t t;
    lv_draw_triangle_dsc_init(&t);
    t.color = acc;
    t.opa = LV_OPA_COVER;
    int tw = r_out > 90 ? 7 : 4;
    int td = r_out > 90 ? 16 : 9;
    t.p[0].x = cx;      t.p[0].y = cy - r_out + 3;
    t.p[1].x = cx - tw; t.p[1].y = cy - r_out + td;
    t.p[2].x = cx + tw; t.p[2].y = cy - r_out + td;
    lv_draw_triangle(layer, &t);

    if (show_sweep && !dim) {
        double end = 270 + sec * 6.0;
        lv_draw_arc_dsc_t ad;
        lv_draw_arc_dsc_init(&ad);
        ad.color = acc;
        ad.width = r_out > 90 ? 3 : 2;
        ad.center.x = cx; ad.center.y = cy;
        ad.radius = r_in;
        ad.start_angle = 270;
        ad.end_angle = (lv_value_precise_t)end;
        ad.opa = LV_OPA_40;
        ad.rounded = 1;
        lv_draw_arc(layer, &ad);

        double ar = end * M_PI / 180.0;
        int dx = cx + (int)(cos(ar) * r_in);
        int dy = cy + (int)(sin(ar) * r_in);
        int dr = r_out > 90 ? 5 : 3;
        lv_draw_rect_dsc_t rd;
        lv_draw_rect_dsc_init(&rd);
        rd.bg_color = acc;
        rd.bg_opa = LV_OPA_COVER;
        rd.radius = LV_RADIUS_CIRCLE;
        lv_area_t dot = { dx - dr, dy - dr, dx + dr, dy + dr };
        lv_draw_rect(layer, &rd, &dot);
    }
}

static void ring_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t   *obj   = lv_event_get_target_obj(e);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    draw_compass(layer, area.x1 + CX, area.y1 + CY, R_OUT,
                 S.dim, S.night, true, S.now.tm_sec);
}

static lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *font,
                          lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, txt);
    return l;
}

static void mk_comp(int idx, lv_align_t align, int dx, int dy,
                    const char *cap, const char *val)
{
    lv_obj_t *box = lv_obj_create(S.root);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_align(box, align, dx, dy);
    bool right = (align == LV_ALIGN_TOP_RIGHT || align == LV_ALIGN_BOTTOM_RIGHT);
    lv_obj_set_style_pad_row(box, 2, 0);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START,
        right ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    S.comp_lbl[idx] = mk_label(box, &lv_font_montserrat_14, COL_INK2, cap);
    S.comp_val[idx] = mk_label(box, &lv_font_montserrat_28, COL_WHITE, val);
}

void wayfinder_build(lv_obj_t *parent)
{
    memset(&S, 0, sizeof S);
    S.root = parent;
    lv_obj_remove_style_all(parent);
    lv_obj_set_size(parent, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    S.ring = lv_obj_create(parent);
    lv_obj_remove_style_all(S.ring);
    lv_obj_set_size(S.ring, SCR_W, SCR_H);
    lv_obj_set_pos(S.ring, 0, 0);
    lv_obj_clear_flag(S.ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(S.ring, ring_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    const char *cn[4] = { "N", "E", "S", "W" };
    int cx_[4] = { CX,             CX + R_OUT - 31, CX,             CX - R_OUT + 31 };
    int cy_[4] = { CY - R_OUT + 31, CY,             CY + R_OUT - 31, CY };
    for (int i = 0; i < 4; i++) {
        S.card[i] = mk_label(parent, &lv_font_montserrat_16,
                             i == 0 ? wf_accent() : COL_INK2, cn[i]);
        lv_obj_update_layout(S.card[i]);
        lv_obj_set_pos(S.card[i], cx_[i] - lv_obj_get_width(S.card[i]) / 2,
                                  cy_[i] - lv_obj_get_height(S.card[i]) / 2);
    }

    lv_obj_t *centre = lv_obj_create(parent);
    lv_obj_remove_style_all(centre);
    lv_obj_set_size(centre, 360, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(centre, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(centre, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(centre, LV_ALIGN_CENTER, 0, -6);

    lv_obj_t *timerow = lv_obj_create(centre);
    lv_obj_remove_style_all(timerow);
    lv_obj_set_size(timerow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(timerow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(timerow, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(timerow, 4, 0);
    S.time_lbl = mk_label(timerow, &lv_font_montserrat_clock_96, COL_WHITE, "10:42");
    S.sec_lbl  = mk_label(timerow, &lv_font_montserrat_28, wf_accent(), "08");

    S.bearing_lbl = mk_label(centre, &lv_font_montserrat_14, COL_INK2,
                             "BEARING 342\xC2\xB0 NW");
    lv_obj_set_style_pad_top(S.bearing_lbl, 8, 0);

    S.date_lbl = mk_label(parent, &lv_font_montserrat_16, wf_accent(), "TUE 20");
    lv_obj_align(S.date_lbl, LV_ALIGN_TOP_MID, 0, 26);

    mk_comp(0, LV_ALIGN_TOP_LEFT,      22, 60, "BATT",  "78%");
    mk_comp(1, LV_ALIGN_TOP_RIGHT,    -22, 60, "TEMP",  "24\xC2\xB0");
    mk_comp(2, LV_ALIGN_BOTTOM_LEFT,   22,-34, "STEPS", "6,432");
    mk_comp(3, LV_ALIGN_BOTTOM_RIGHT, -22,-34, "HEART", "72");

    S.built = true;
}

void wayfinder_update(const struct tm *t)
{
    if (!S.built || !t) return;
    S.now = *t;
    static const char *days[7] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
    char buf[16];
    snprintf(buf, sizeof buf, "%02d:%02d", t->tm_hour, t->tm_min);
    lv_label_set_text(S.time_lbl, buf);
    snprintf(buf, sizeof buf, "%02d", t->tm_sec);
    lv_label_set_text(S.sec_lbl, buf);
    int wd = t->tm_wday % 7;
    snprintf(buf, sizeof buf, "%s %d", days[wd], t->tm_mday);
    lv_label_set_text(S.date_lbl, buf);
    lv_obj_invalidate(S.ring);
}

static void wf_restyle(void)
{
    if (!S.built) return;
    lv_color_t acc = wf_accent(), tk = wf_ink();
    lv_obj_set_style_text_color(S.time_lbl, S.dim ? COL_INK2 : tk, 0);
    lv_obj_set_style_text_color(S.date_lbl, acc, 0);
    lv_obj_set_style_text_color(S.card[0], acc, 0);
    lv_obj_set_style_text_color(S.sec_lbl, acc, 0);
    lv_opa_t hide = S.dim ? LV_OPA_TRANSP : LV_OPA_COVER;
    lv_obj_set_style_opa(S.sec_lbl, hide, 0);
    lv_obj_set_style_opa(S.bearing_lbl, hide, 0);
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_opa(S.comp_lbl[i], hide, 0);
        lv_obj_set_style_opa(S.comp_val[i], hide, 0);
        lv_obj_set_style_text_color(S.comp_val[i], tk, 0);
    }
    for (int i = 1; i < 4; i++)
        lv_obj_set_style_text_color(S.card[i], S.night ? COL_RED_INK : COL_INK2, 0);
    lv_obj_invalidate(S.ring);
}

void wayfinder_set_dim(bool on)   { S.dim = on;   wf_restyle(); }
void wayfinder_set_night(bool on) { S.night = on; wf_restyle(); }

/* ---- preview emblem for the face picker ---- */
static void preview_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t   *obj   = lv_event_get_target_obj(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    int cx = (a.x1 + a.x2) / 2;
    int cy = (a.y1 + a.y2) / 2;
    int r  = (lv_area_get_width(&a) / 2) - 4;
    draw_compass(layer, cx, cy, r, false, false, true, 40);
}

lv_obj_t *wayfinder_build_preview(lv_obj_t *parent, int diameter)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, diameter, diameter);
    lv_obj_set_style_bg_color(box, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(box, preview_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    lv_obj_t *t = lv_label_create(box);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_label_set_text(t, "10:42");
    lv_obj_center(t);
    return box;
}

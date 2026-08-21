/*
 * Wayfinder face — see wayfinder_face.h.
 * Curved corner complications (battery, steps) hug the screen's rounded corners
 * following an arc concentric with the compass dial, so nothing is clipped.
 */
#include "wayfinder_face.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern "C" const lv_font_t lv_font_montserrat_clock_96;

#define SCR_W   410
#define SCR_H   502
#define CX      205
#define CY      250
#define R_OUT   168
#define R_IN    (R_OUT - 46)
#define R_COMP  206

#define COL_ACCENT    lv_color_hex(0xff7b2e)
#define COL_ACCENT_R  lv_color_hex(0xff3535)
#define COL_WHITE     lv_color_hex(0xffffff)
#define COL_INK2      lv_color_hex(0x9a9aa2)
#define COL_RED_INK   lv_color_hex(0xff6a6a)

typedef struct {
    lv_obj_t *root, *ring, *time_lbl, *sec_lbl, *date_lbl;
    lv_obj_t *card[4];
    lv_obj_t *batt_cont, *steps_cont;
    struct tm now;
    int  batt_pct, shown_batt;
    long steps, shown_steps;
    bool shown_dim;
    bool built, dim, night;
} wf_t;

static wf_t S;

static lv_color_t wf_accent(void) { return S.night ? COL_ACCENT_R : COL_ACCENT; }

/* ---- compass ring (decorative) ---- */
static void ring_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t   *obj   = lv_event_get_target_obj(e);
    lv_area_t a; lv_obj_get_coords(obj, &a);
    int cx = a.x1 + CX, cy = a.y1 + CY;
    lv_color_t acc = wf_accent();

    for (int i = 0; i < 60; i++) {
        double an = (i * 6 - 90) * M_PI / 180.0;
        int major = (i % 5 == 0);
        int r2 = R_OUT - (major ? 18 : 9);
        lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
        d.color = S.night ? COL_RED_INK : COL_WHITE;
        d.width = major ? 3 : 1; d.round_start = d.round_end = 1;
        d.opa = S.dim ? (major ? LV_OPA_50 : LV_OPA_10) : (major ? LV_OPA_90 : LV_OPA_30);
        d.p1.x = cx + (lv_value_precise_t)(cos(an) * R_OUT);
        d.p1.y = cy + (lv_value_precise_t)(sin(an) * R_OUT);
        d.p2.x = cx + (lv_value_precise_t)(cos(an) * r2);
        d.p2.y = cy + (lv_value_precise_t)(sin(an) * r2);
        lv_draw_line(layer, &d);
    }
    lv_draw_triangle_dsc_t t; lv_draw_triangle_dsc_init(&t);
    t.color = acc; t.opa = LV_OPA_COVER;
    t.p[0].x = cx;     t.p[0].y = cy - R_OUT + 3;
    t.p[1].x = cx - 7; t.p[1].y = cy - R_OUT + 16;
    t.p[2].x = cx + 7; t.p[2].y = cy - R_OUT + 16;
    lv_draw_triangle(layer, &t);

    if (!S.dim) {
        double e2 = 270 + 6.0 * (double)S.now.tm_sec;
        lv_draw_arc_dsc_t ad; lv_draw_arc_dsc_init(&ad);
        ad.color = acc; ad.width = 3; ad.center.x = cx; ad.center.y = cy;
        ad.radius = R_IN; ad.start_angle = 270; ad.end_angle = (lv_value_precise_t)e2;
        ad.opa = LV_OPA_40; ad.rounded = 1;
        lv_draw_arc(layer, &ad);
        double ar = e2 * M_PI / 180.0;
        int dx = cx + (int)(cos(ar) * R_IN), dy = cy + (int)(sin(ar) * R_IN);
        lv_draw_rect_dsc_t rd; lv_draw_rect_dsc_init(&rd);
        rd.bg_color = acc; rd.bg_opa = LV_OPA_COVER; rd.radius = LV_RADIUS_CIRCLE;
        lv_area_t dot = { dx - 5, dy - 5, dx + 5, dy + 5 };
        lv_draw_rect(layer, &rd, &dot);
    }
}

/* ---- curved text along an arc concentric with the dial ---- */
static void set_curved_text(lv_obj_t *cont, const char *txt, const lv_font_t *font,
                            lv_color_t col, float radius, float center_deg, float step_deg)
{
    lv_obj_clean(cont);
    int n = (int)strlen(txt);
    for (int i = 0; i < n; i++) {
        float th = center_deg + step_deg * ((n - 1) / 2.0f - i);
        float r  = th * (float)M_PI / 180.0f;
        int x = (int)(CX + radius * cosf(r));
        int y = (int)(CY + radius * sinf(r));
        char s[2] = { txt[i], 0 };
        lv_obj_t *g = lv_label_create(cont);
        lv_obj_set_style_text_font(g, font, 0);
        lv_obj_set_style_text_color(g, col, 0);
        lv_label_set_text(g, s);
        lv_obj_update_layout(g);
        int gw = lv_obj_get_width(g), gh = lv_obj_get_height(g);
        lv_obj_set_style_transform_pivot_x(g, gw / 2, 0);
        lv_obj_set_style_transform_pivot_y(g, gh / 2, 0);
        lv_obj_set_style_transform_angle(g, (int)((th - 90.0f) * 10), 0);
        lv_obj_set_pos(g, x - gw / 2, y - gh / 2);
    }
}

/* thousands-separated step count, or "--" if unavailable */
static void fmt_steps(long n, char *out, size_t sz)
{
    if (n < 0) { snprintf(out, sz, "--"); return; }
    char raw[16];
    snprintf(raw, sizeof raw, "%ld", n);
    int len = (int)strlen(raw), o = 0;
    for (int i = 0; i < len && o < (int)sz - 1; i++) {
        if (i > 0 && (len - i) % 3 == 0) out[o++] = ',';
        out[o++] = raw[i];
    }
    out[o] = 0;
}

static lv_obj_t *mk_curve_container(void)
{
    lv_obj_t *c = lv_obj_create(S.root);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, SCR_W, SCR_H);
    lv_obj_set_pos(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, txt);
    return l;
}

void wayfinder_build(lv_obj_t *parent)
{
    memset(&S, 0, sizeof S);
    S.root = parent;
    S.batt_pct = -1;
    S.steps = -1;
    lv_obj_remove_style_all(parent);
    lv_obj_set_size(parent, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    S.ring = lv_obj_create(parent);
    lv_obj_remove_style_all(S.ring);
    lv_obj_set_size(S.ring, SCR_W, SCR_H);
    lv_obj_add_event_cb(S.ring, ring_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    const char *cn[4] = { "N", "E", "S", "W" };
    int cx_[4] = { CX, CX + R_OUT - 31, CX, CX - R_OUT + 31 };
    int cy_[4] = { CY - R_OUT + 31, CY, CY + R_OUT - 31, CY };
    for (int i = 0; i < 4; i++) {
        S.card[i] = mk_label(parent, &lv_font_montserrat_16, i == 0 ? wf_accent() : COL_INK2, cn[i]);
        lv_obj_update_layout(S.card[i]);
        lv_obj_set_pos(S.card[i], cx_[i] - lv_obj_get_width(S.card[i]) / 2,
                                  cy_[i] - lv_obj_get_height(S.card[i]) / 2);
    }

    lv_obj_t *centre = lv_obj_create(parent);
    lv_obj_remove_style_all(centre);
    lv_obj_set_size(centre, 380, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(centre, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(centre, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(centre, 4, 0);
    lv_obj_align(centre, LV_ALIGN_CENTER, 0, -4);
    S.time_lbl = mk_label(centre, &lv_font_montserrat_clock_96, COL_WHITE, "10:42");
    S.sec_lbl  = mk_label(centre, &lv_font_montserrat_28, wf_accent(), "08");

    S.date_lbl = mk_label(parent, &lv_font_montserrat_16, wf_accent(), "TUE 20");
    lv_obj_align(S.date_lbl, LV_ALIGN_TOP_MID, 0, 26);

    S.batt_cont  = mk_curve_container();
    S.steps_cont = mk_curve_container();

    S.built = true;
}

void wayfinder_set_stats(int batt_pct, long steps)
{
    S.batt_pct = batt_pct;
    S.steps = steps;
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
    snprintf(buf, sizeof buf, "%s %d", days[t->tm_wday % 7], t->tm_mday);
    lv_label_set_text(S.date_lbl, buf);

    // Rebuilding curved text means destroying + recreating ~8 glyph labels, so
    // only do it when a value (or the dim palette) actually changed — not every
    // second. This keeps the once-a-second tick cheap. (battery win)
    if (S.batt_pct != S.shown_batt || S.steps != S.shown_steps || S.dim != S.shown_dim) {
        lv_color_t vc = S.dim ? COL_INK2 : (S.night ? COL_RED_INK : COL_WHITE);
        char batt[12], steps[16];
        if (S.batt_pct < 0) snprintf(batt, sizeof batt, "--");
        else                snprintf(batt, sizeof batt, "%d%%", S.batt_pct);
        fmt_steps(S.steps, steps, sizeof steps);
        set_curved_text(S.batt_cont,  batt,  &lv_font_montserrat_28, vc, R_COMP, 137.0f, 6.5f);
        set_curved_text(S.steps_cont, steps, &lv_font_montserrat_28, vc, R_COMP, 43.0f, 6.5f);
        lv_obj_set_style_opa(S.batt_cont,  S.dim ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_opa(S.steps_cont, S.dim ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        S.shown_batt = S.batt_pct;
        S.shown_steps = S.steps;
        S.shown_dim = S.dim;
    }

    // The second-sweep only animates when bright; in dim/AOD there's nothing
    // moving, so don't repaint the ring every second. (battery win)
    if (!S.dim) lv_obj_invalidate(S.ring);
}

static void wf_restyle(void)
{
    if (!S.built) return;
    lv_color_t acc = wf_accent();
    lv_obj_set_style_text_color(S.time_lbl, S.dim ? COL_INK2 : (S.night ? COL_RED_INK : COL_WHITE), 0);
    lv_obj_set_style_text_color(S.date_lbl, acc, 0);
    lv_obj_set_style_text_color(S.card[0], acc, 0);
    lv_obj_set_style_text_color(S.sec_lbl, acc, 0);
    lv_obj_set_style_opa(S.sec_lbl, S.dim ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    for (int i = 1; i < 4; i++)
        lv_obj_set_style_text_color(S.card[i], S.night ? COL_RED_INK : COL_INK2, 0);
    wayfinder_update(&S.now);
    lv_obj_invalidate(S.ring);   // force one repaint on dim/night transition
}

void wayfinder_set_dim(bool on)   { S.dim = on;   wf_restyle(); }
void wayfinder_set_night(bool on) { S.night = on; wf_restyle(); }

/* ---- preview emblem for the face picker ---- */
static void preview_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t   *obj   = lv_event_get_target_obj(e);
    lv_area_t a; lv_obj_get_coords(obj, &a);
    int cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
    int r  = (lv_area_get_width(&a) / 2) - 4;
    for (int i = 0; i < 60; i++) {
        double an = (i * 6 - 90) * M_PI / 180.0;
        int major = (i % 5 == 0);
        int r2 = r - (major ? r * 18 / 168 : r * 9 / 168);
        lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
        d.color = lv_color_white(); d.width = major ? 2 : 1;
        d.opa = major ? LV_OPA_80 : LV_OPA_30;
        d.p1.x = cx + (lv_value_precise_t)(cos(an) * r);
        d.p1.y = cy + (lv_value_precise_t)(sin(an) * r);
        d.p2.x = cx + (lv_value_precise_t)(cos(an) * r2);
        d.p2.y = cy + (lv_value_precise_t)(sin(an) * r2);
        lv_draw_line(layer, &d);
    }
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

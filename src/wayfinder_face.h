/*
 * Wayfinder — a compass-instrument watch face for 13:37 (T-Watch Ultra).
 *
 * Ported from the standalone LVGL face (verified in the desktop simulator).
 * Builds into a caller-provided container; the clock screen shows/hides that
 * container to switch faces. Time is pushed in from the caller so it shares
 * 13:37's RTC + timezone handling.
 */
#ifndef WAYFINDER_FACE_H
#define WAYFINDER_FACE_H

#include "lvgl.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Build the full 410x502 Wayfinder face into `parent`. */
void wayfinder_build(lv_obj_t *parent);

/* Push the current time (per second). Safe to call before build (no-op). */
void wayfinder_update(const struct tm *t);

void wayfinder_set_dim(bool on);
void wayfinder_set_night(bool on);

/* A small static compass emblem for the face picker's preview tile. */
lv_obj_t *wayfinder_build_preview(lv_obj_t *parent, int diameter);

#ifdef __cplusplus
}
#endif
#endif

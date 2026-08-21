/*
 * Wayfinder — a compass-instrument watch face for 13:37 (T-Watch Ultra).
 *
 * Compass ring + cardinals are decorative (the Ultra has no magnetometer).
 * Battery and step count are real, pushed in via wayfinder_set_stats().
 */
#ifndef WAYFINDER_FACE_H
#define WAYFINDER_FACE_H

#include "lvgl.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

void wayfinder_build(lv_obj_t *parent);
void wayfinder_update(const struct tm *t);

// Push real sensor values. batt_pct 0..100; steps < 0 means "unavailable".
void wayfinder_set_stats(int batt_pct, long steps);

void wayfinder_set_dim(bool on);
void wayfinder_set_night(bool on);

lv_obj_t *wayfinder_build_preview(lv_obj_t *parent, int diameter);

#ifdef __cplusplus
}
#endif
#endif

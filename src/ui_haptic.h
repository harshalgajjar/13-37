/*
 * Shared UI haptic feedback. ui_haptic_tap() plays a short DRV2605 click for
 * interaction feedback (tile taps, face selection), gated by the user's vibrate
 * setting. Defined in main.cpp.
 */
#ifndef UI_HAPTIC_H
#define UI_HAPTIC_H

#include <lvgl.h>

void ui_haptic_tap();

/* Convenience LVGL event callback: buzz on the event it's registered for. */
static inline void ui_haptic_cb(lv_event_t *e) { (void)e; ui_haptic_tap(); }

#endif

/*
 * Shared on-screen-keyboard styling for the T-Watch Ultra's rounded screen.
 *
 * The panel has rounded corners, so a full-bleed keyboard's corner keys get
 * clipped. This insets the button matrix from the keyboard edges (and lifts the
 * bottom row up) so every key stays inside the visible, non-rounded area. It
 * also adds small gaps between keys, which reads cleaner. Style-only, so it can
 * be called any time after lv_keyboard_create().
 */
#ifndef UI_KBD_H
#define UI_KBD_H

#include <lvgl.h>

static inline void keyboard_fit_corners(lv_obj_t *kb)
{
    lv_obj_set_style_pad_left(kb,    10, LV_PART_MAIN);
    lv_obj_set_style_pad_right(kb,   10, LV_PART_MAIN);
    lv_obj_set_style_pad_top(kb,      6, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(kb,  16, LV_PART_MAIN);  /* clears the bottom corner curve */
    lv_obj_set_style_pad_row(kb,      6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(kb,   5, LV_PART_MAIN);
    lv_obj_set_style_radius(kb,      20, LV_PART_MAIN);
}

#endif

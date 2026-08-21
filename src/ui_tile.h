/*
 * Consistent look + tactile press feedback for the 180x180 selection tiles used
 * by the Tools grid and the Time (alarm/timer/stopwatch) grid. On press a tile
 * brightens, gets an orange edge, and scales down slightly — smoothly animated —
 * so selecting anything feels responsive. Call once after creating the tile.
 */
#ifndef UI_TILE_H
#define UI_TILE_H

#include <lvgl.h>

static inline void tile_apply_style(lv_obj_t *tile)
{
    lv_obj_set_style_border_color(tile, lv_color_make(0x33, 0x33, 0x33), LV_PART_MAIN);
    lv_obj_set_style_radius(tile, 18, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_x(tile, 90, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(tile, 90, LV_PART_MAIN);

    lv_obj_set_style_bg_color(tile, lv_color_make(0x1d, 0x1a, 0x16), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(tile, lv_color_hex(0xff7b2e), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(tile, 2, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_x(tile, 244, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale_y(tile, 244, LV_PART_MAIN | LV_STATE_PRESSED);

    static lv_style_transition_dsc_t s_trans;
    static bool s_init = false;
    if (!s_init) {
        static const lv_style_prop_t props[] = {
            LV_STYLE_TRANSFORM_SCALE_X, LV_STYLE_TRANSFORM_SCALE_Y,
            LV_STYLE_BORDER_COLOR, LV_STYLE_BORDER_WIDTH, LV_STYLE_BG_COLOR,
            (lv_style_prop_t)0 };
        lv_style_transition_dsc_init(&s_trans, props, lv_anim_path_ease_out, 130, 0, NULL);
        s_init = true;
    }
    lv_obj_set_style_transition(tile, &s_trans, LV_PART_MAIN);
}

#endif

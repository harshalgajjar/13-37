#pragma once
#include <lvgl.h>

void settings_screen_create();
void settings_screen_show();
bool settings_screen_is_active();

// Apply + persist a watch-face choice (0=Digital, 1=Analog, 2=Wayfinder).
// Called by the swipeable face picker when the user confirms a face.
void settings_apply_face(int mode);

// Read /Settings/settings.txt from the SD card (if mounted) and apply each
// value to the widgets and clock-screen setters. Safe to call when no card
// or no file exists — it just does nothing.
void settings_screen_load();

// Read by the screenshot module's poll loop to decide whether a 3 s touch
// should fire a capture. Returns the AND of the user's "Screenshot long
// press" switch state and SD availability — so when the card unmounts the
// feature goes silent even if the switch hasn't been visibly cleared yet.
bool settings_get_screenshot_long_press();

// Re-evaluate the SD-dependent state of the screenshot row: enables the
// switch when a card is mounted and disables (greys + forces off) when
// the card is gone or USB-SD has claimed it. Called from the main loop's
// SD-state-change branch and once at boot, so the toggle never lies
// about whether the feature can actually write a file right now.
void settings_screen_apply_sd_state();

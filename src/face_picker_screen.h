/*
 * Swipeable watch-face picker. Full-screen; swipe left/right through the
 * available faces and tap SELECT to apply. Reached from Settings → Watch Face.
 */
#ifndef FACE_PICKER_SCREEN_H
#define FACE_PICKER_SCREEN_H

// Open the picker, centred on `current_mode` (0=Digital, 1=Analog, 2=Wayfinder).
void face_picker_show(int current_mode);

#endif

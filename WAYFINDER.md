# Wayfinder fork — changes & on-device test plan

This fork of [r3dfish/13-37](https://github.com/r3dfish/13-37) adds a custom
**Wayfinder** watch face and a pass of UX + battery improvements, on branch
`wayfinder-face`. Everything below builds clean; this doc is the checklist for
verifying it on the actual T-Watch Ultra after flashing.

## What was added

### Watch face + selection
- **Wayfinder face** (`wayfinder_face.cpp`): a compass-instrument dial — tick
  ring, cardinals, sweeping orange second marker, big time (reuses 13:37's 96px
  clock font). Compass is decorative (the Ultra has **no magnetometer**).
- **Real data only**: battery from `pmu.getBatteryPercent()`; steps from the
  BHI260AP onboard step counter. No fake temp / heart / bearing.
- **Curved corner complications**: battery + steps are laid out as text on an arc
  concentric with the dial, so they hug the rounded corners instead of clipping.
- **Swipeable face picker** (`face_picker_screen.cpp`): Settings → Watch Face ›
  → swipe Digital / Analog / Wayfinder → SELECT. Shows an "IN USE" badge and the
  SELECT button names the face. Choice persists (`face_mode` key; migrates the
  old `analog_face`).

### UX
- **Global fade transitions** between all screens (boot splash stays instant).
- **Tactile selection tiles**: Tools + Time grids scale + orange-edge on press,
  smoothly animated (`ui_tile.h`).
- **Haptic feedback** on tile taps and face selection (`ui_haptic.h`, gated by
  the vibrate setting).
- **Keyboard keys clear the rounded corners** across all 8 keyboards
  (`ui_kbd.h`).

### Battery
- Wayfinder rebuilds curved complications only on value change, not every second.
- No per-second ring repaint while dimmed.
- Wayfinder enters a low-power AOD look on idle-dim (sweep off, complications
  hidden).
- Idle/dimmed main loop paces down (1 render pass + 30 ms vs 3 passes + 2 ms).
- Skips hidden status-indicator polling while the Wayfinder overlay covers them.
- Skips the clock/Wayfinder rebuild entirely while a tool screen is showing.

## On-device verification checklist

After flashing (see below), press RST and check:

**Face**
- [ ] Settings → Watch Face › opens the swipeable picker; dots + "USE <name>" +
      "IN USE" badge all track correctly.
- [ ] Select Wayfinder → it appears; time, date, second-sweep all live.
- [ ] **Battery** reads a real % (matches the digital face's battery).
- [ ] **Steps** shows a real, increasing count when you walk — NOT `--`.
      (If `--`, the loaded BHI260 firmware lacks the step counter → serial prints
      `[wayfinder] step counter: unavailable`; fallback is accel-based counting.)
- [ ] **Corners**: battery (bottom-left) + steps (bottom-right) are fully visible,
      nothing clipped. If a digit still nicks, bump `R_COMP` / the pad values.

**UX**
- [ ] Screen changes cross-fade smoothly (not instant cuts).
- [ ] Tool tiles + alarm/timer tiles visibly press (scale + orange edge) and buzz
      (if vibrate is on).
- [ ] Open any text field (e.g. WiFi password, APRS) — no keyboard key is clipped
      by a rounded corner.

**Battery** (needs hours, not seconds)
- [ ] Leave on the Wayfinder face and let it idle-dim; confirm it drops to the
      minimal look and the watch runs meaningfully longer than stock over a day.

## Build & flash (contained toolchain)

```sh
# Build (PlatformIO core + toolchain live in ./.pio-core, inside the project)
PLATFORMIO_CORE_DIR="$PWD/.pio-core" ../.venv/bin/pio run -e twatch_ultra

# Flash: put the watch in download mode first —
#   hold BOOT → click RST → release BOOT
# then (from the repo root):
B=.pio/build/twatch_ultra
BOOT0=.pio-core/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin
../.venv/bin/esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX --before no-reset \
  --after hard-reset write-flash -z --flash-mode keep --flash-freq keep --flash-size keep \
  0x0 "$B/bootloader.bin" 0x8000 "$B/partitions.bin" 0xe000 "$BOOT0" 0x10000 "$B/firmware.bin"
# then press RST on the watch.
```

Notes for this board: use `--before no-reset` (in download mode) or `usb-reset`;
never pass `--baud 921600` (corrupts transfers over native USB). After flashing,
the "Hard resetting" message is a no-op — press RST by hand.

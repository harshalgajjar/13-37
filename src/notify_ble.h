/*
 * Notification link — a Nordic-UART BLE peripheral that speaks Gadgetbridge's
 * Bangle.js protocol, so an Android phone running Gadgetbridge mirrors its
 * notifications (and, later, calls + replies) to the watch.
 *
 * Coexistence: this OWNS the BLE radio while active. The BLE scan tools
 * (AirTag/Flipper/Flock) and the BLE mouse assume they own BLE too, so for now
 * Notifications is mutually exclusive with them — starting one stops the other.
 */
#ifndef NOTIFY_BLE_H
#define NOTIFY_BLE_H

#include <Arduino.h>

#define NOTIFY_MAX      24     /* ring buffer of recent notifications */
#define NOTIFY_TXT_MAX  120
#define NOTIFY_PIN      133700 /* static BLE pairing passkey (shown on watch) */

typedef struct {
    int32_t id;
    char    src[24];    /* app name, e.g. "Messages", "WhatsApp" */
    char    title[48];  /* sender / title */
    char    body[NOTIFY_TXT_MAX];
    bool    used;
} notif_t;

/* Lifecycle */
bool notify_ble_begin(void);      /* start advertising; false if a scanner holds BLE */
void notify_ble_stop(void);       /* tear down, release the BLE radio */
bool notify_ble_active(void);     /* is the link advertising/connected right now? */
bool notify_ble_connected(void);  /* is a phone connected? */
bool notify_ble_is_built(void);   /* has the BLE stack been constructed this boot? */
bool notify_ble_paired(void);     /* did the last pairing complete (PIN accepted)? */

/* Incoming-call controls (driven by the call banner). */
bool notify_ble_call_ringing(void); /* is a call currently ringing on the watch? */
void notify_ble_call_silence(void); /* stop the watch ring; phone keeps ringing  */
void notify_ble_call_reject(void);  /* decline the call on the phone + stop ring  */
void notify_ble_find_phone(bool on);/* ring the phone via Gadgetbridge (on/off)   */

/* Diagnostics (shown on the Notify screen while debugging the link). */
uint32_t notify_ble_connects(void);     /* # of GATT connects seen        */
uint32_t notify_ble_disconnects(void);  /* # of disconnects               */
uint8_t  notify_ble_last_reason(void);  /* last disconnect reason code    */
uint16_t notify_ble_conn_interval_ms(void); /* live connection interval (opt #2) */
uint32_t notify_ble_rx_bytes(void);     /* total bytes received from phone */
uint32_t notify_ble_total_added(void);  /* monotonic count of stored notifs */
void notify_ble_loop(void);       /* drain RX buffer; call from main loop */
void notify_ble_keepalive(void);  /* rebuild the link after WiFi/scanners free the radio */

/* Show a popup banner for newly-arrived notifications; call from main loop.
   Works over any active screen (draws on the LVGL top layer). */
void notify_ui_poll(void);

/* Send a raw JSON line to the phone (adds the trailing newline). */
void notify_ble_send_line(const char *json);

/* User alert preferences (persisted to SD). Vibrate on by default, sound off,
 * DND off. DND makes the watch ignore ALL incoming notifications and calls. */
bool notify_get_vibrate(void);  void notify_set_vibrate(bool on);
bool notify_get_sound(void);    void notify_set_sound(bool on);
bool notify_get_dnd(void);      void notify_set_dnd(bool on);

/* Store access for the UI (defined in notify_ble.cpp). */
int            notify_count(void);
const notif_t *notify_get(int idx);         /* newest first; idx 0..count-1 */
void           notify_clear_all(void);

#endif

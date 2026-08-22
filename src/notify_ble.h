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
void notify_ble_begin(void);      /* init + advertise as a Bangle.js device */
void notify_ble_stop(void);       /* tear down, release the BLE radio */
bool notify_ble_active(void);     /* is the link enabled? */
bool notify_ble_connected(void);  /* is a phone connected? */

/* Diagnostics (shown on the Notify screen while debugging the link). */
uint32_t notify_ble_connects(void);     /* # of GATT connects seen        */
uint32_t notify_ble_disconnects(void);  /* # of disconnects               */
uint8_t  notify_ble_last_reason(void);  /* last disconnect reason code    */
void notify_ble_loop(void);       /* drain RX buffer; call from main loop */

/* Send a raw JSON line to the phone (adds the trailing newline). */
void notify_ble_send_line(const char *json);

/* Store access for the UI (defined in notify_ble.cpp). */
int            notify_count(void);
const notif_t *notify_get(int idx);         /* newest first; idx 0..count-1 */
void           notify_clear_all(void);

#endif

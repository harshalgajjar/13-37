/*
 * Notification link over BLE Nordic-UART, Gadgetbridge Bangle.js protocol.
 * See notify_ble.h. Milestone 1: receive + store notifications (mirroring).
 */
#include "notify_ble.h"
#include <LilyGoLib.h>          /* instance.vibrator() — haptic ring on incoming call */
#include <SD.h>                 /* persist alert settings                         */
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include <esp_gap_ble_api.h>   /* esp_ble_gap_set_security_param, static passkey */
#include <esp_bt.h>            /* esp_bt_controller_enable/disable                 */
#include <esp_bt_main.h>       /* esp_bluedroid_enable/disable (reversible)       */
#include <WiFi.h>              /* WiFi.getMode — WiFi tools need the BLE radio off */
#include <string.h>
#include "ble_scan_manager.h"   /* ble_scan_active() — scanner arbitration */
#include "mouse_hid.h"          /* take over the BLE radio from the mouse   */
#include "alarm.h"              /* alarm_play_chime_loop() — reuse for the ringtone */

extern void display_wake(void); /* main.cpp — un-dim so the banner is visible */

/* ---- alert settings (vibrate / sound / do-not-disturb), persisted to SD ---- */
#define NOTIFY_CFG_PATH "/notify.cfg"
static bool s_vibrate = true;    /* buzz on notifications + calls   */
static bool s_sound   = false;   /* play a ringtone/chime on alerts  */
static bool s_dnd     = false;   /* ignore ALL incoming alerts       */
static bool s_cfg_loaded = false;

static void settings_save(void)
{
    if (!instance.isCardReady()) return;
    File f = SD.open(NOTIFY_CFG_PATH, FILE_WRITE);
    if (!f) return;
    f.printf("vibrate=%d\nsound=%d\ndnd=%d\n", s_vibrate ? 1 : 0,
             s_sound ? 1 : 0, s_dnd ? 1 : 0);
    f.close();
}

static void settings_load(void)
{
    if (s_cfg_loaded || !instance.isCardReady()) return;
    s_cfg_loaded = true;               /* only attempt once the card is ready */
    File f = SD.open(NOTIFY_CFG_PATH, FILE_READ);
    if (!f) return;
    char line[32];
    while (f.available()) {
        size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
        line[n] = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        bool v = strtol(eq + 1, nullptr, 10) != 0;
        if      (strcmp(line, "vibrate") == 0) s_vibrate = v;
        else if (strcmp(line, "sound")   == 0) s_sound   = v;
        else if (strcmp(line, "dnd")     == 0) s_dnd     = v;
    }
    f.close();
}

bool notify_get_vibrate(void) { settings_load(); return s_vibrate; }
bool notify_get_sound(void)   { settings_load(); return s_sound;   }
bool notify_get_dnd(void)     { settings_load(); return s_dnd;     }
void notify_set_vibrate(bool on) { s_vibrate = on; settings_save(); }
void notify_set_sound(bool on)   { s_sound = on;   settings_save();
                                   if (!on) alarm_stop_chime_loop(); }
void notify_set_dnd(bool on)     { s_dnd = on;     settings_save(); }

/* Secure pairing: require a bonded, MITM-protected link. The watch DISPLAYS a
 * fixed passkey (NOTIFY_PIN) which the phone must type in — so a random device
 * can't silently connect and read your notifications. */
static volatile bool s_paired    = false;   /* last pairing succeeded  */
static volatile bool s_pair_fail = false;   /* last pairing was rejected */

class SecCb : public BLESecurityCallbacks {
    /* IO_CAP_OUT means the *phone* enters the key, so onPassKeyRequest (device-
     * as-input) isn't used; return the PIN anyway as a harmless fallback. */
    uint32_t onPassKeyRequest() override { return NOTIFY_PIN; }
    /* Stack tells us the key to show; with a static passkey it's NOTIFY_PIN. */
    void     onPassKeyNotify(uint32_t) override {}
    bool     onSecurityRequest() override { return true; }   /* accept pairing */
    bool     onConfirmPIN(uint32_t) override { return true; }
    void     onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override {
        s_paired    =  cmpl.success;
        s_pair_fail = !cmpl.success;
    }
};

bool notify_ble_paired(void) { return s_paired; }

/* Sentinel id for the single live-call entry, so end/accept/reject can find and
 * clear it regardless of the notification id the phone used. */
#define CALL_NOTIF_ID  0x7FFFFFFF

/* Incoming-call ring: pulse the motor (and, if enabled, loop the chime) every
 * INTERVAL while the phone rings, capped so a missed "end" can't buzz forever.
 * A one-shot notification alert (SMS etc.) reuses the same chime but stops it
 * after a short beep. */
#define CALL_BUZZ_INTERVAL_MS  1200
#define CALL_RING_TIMEOUT_MS   45000
#define NOTIF_CHIME_MS         1400   /* one-shot chime length for a notification */
static bool     s_call_ringing   = false;
static uint32_t s_ring_start_ms  = 0;
static uint32_t s_last_buzz_ms   = 0;
static uint32_t s_notif_chime_off_ms = 0;  /* 0 = no one-shot chime pending */

static void call_ring_start(void)
{
    if (s_call_ringing) return;   /* already ringing — don't reset the cadence */
    s_call_ringing  = true;
    s_ring_start_ms = millis();
    s_last_buzz_ms  = 0;          /* 0 => buzz on the very next tick */
    if (s_sound) alarm_play_chime_loop(0);   /* doorbell loop until the call clears */
    display_wake();
}

static void call_ring_stop(void)
{
    s_call_ringing = false;
    alarm_stop_chime_loop();
}

/* One-shot alert for a stored notification (not a call): a single buzz + a
 * brief chime + wake the screen so the banner is seen. */
static void notify_alert_once(void)
{
    if (s_vibrate) instance.vibrator();
    if (s_sound && !s_call_ringing) {
        alarm_play_chime_loop(0);
        s_notif_chime_off_ms = millis() + NOTIF_CHIME_MS;
    }
    display_wake();
}

/* Called every main-loop iteration from notify_ble_loop(). */
static void call_ring_tick(void)
{
    uint32_t now = millis();

    /* Stop a one-shot notification chime once its window elapses (unless a call
     * ring has since taken over the chime — then the ring owns it). */
    if (s_notif_chime_off_ms && (int32_t)(now - s_notif_chime_off_ms) >= 0) {
        s_notif_chime_off_ms = 0;
        if (!s_call_ringing) alarm_stop_chime_loop();
    }

    if (!s_call_ringing) return;
    if (now - s_ring_start_ms > CALL_RING_TIMEOUT_MS) { call_ring_stop(); return; }
    if (s_vibrate && (s_last_buzz_ms == 0 || now - s_last_buzz_ms >= CALL_BUZZ_INTERVAL_MS)) {
        s_last_buzz_ms = now;
        instance.vibrator();      /* one haptic pulse (DRV2605) */
    }
}

/* Call actions from the incoming-call banner (declared in notify_ble.h). */
bool notify_ble_call_ringing(void) { return s_call_ringing; }

/* Silence: stop the watch's ring/vibration only. The phone keeps ringing so the
 * user can still answer it there. */
void notify_ble_call_silence(void) { call_ring_stop(); }

/* Reject: decline the call on the phone, then stop the local ring. Use "END"
 * (23 bytes with newline) rather than "REJECT" (26) so the message fits in a
 * single notification even at the minimum negotiated MTU; Gadgetbridge maps END
 * to hanging up, which declines a still-ringing call. */
void notify_ble_call_reject(void)
{
    notify_ble_send_line("{\"t\":\"call\",\"n\":\"END\"}");
    call_ring_stop();
}

/* Find Phone: ask Gadgetbridge to ring the phone (n:true) or stop (n:false). */
void notify_ble_find_phone(bool on)
{
    notify_ble_send_line(on ? "{\"t\":\"findPhone\",\"n\":true}"
                            : "{\"t\":\"findPhone\",\"n\":false}");
}

/* Nordic UART Service */
#define NUS_SVC "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX  "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  /* phone -> watch (write)  */
#define NUS_TX  "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  /* watch -> phone (notify) */

static BLEServer         *s_server = nullptr;
static BLECharacteristic *s_tx     = nullptr;
static bool  s_active    = false;
static bool  s_connected = false;

/* RX byte buffer, filled from the BLE task, drained in notify_ble_loop(). */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static char   s_rx[2048];
static volatile size_t s_rx_len = 0;

/* ---- notification store (ring buffer, newest first via s_head) ---- */
static notif_t s_store[NOTIFY_MAX];
static int      s_head  = 0;   /* index of next slot to write */
static int      s_count = 0;
static uint32_t s_total_added = 0;   /* monotonic: how many ever stored (for popups) */

static void store_add(const notif_t *n)
{
    s_store[s_head] = *n;
    s_store[s_head].used = true;
    s_head = (s_head + 1) % NOTIFY_MAX;
    if (s_count < NOTIFY_MAX) s_count++;
    s_total_added++;
}

uint32_t notify_ble_total_added(void) { return s_total_added; }

static void store_dismiss(int32_t id)
{
    for (int i = 0; i < NOTIFY_MAX; i++)
        if (s_store[i].used && s_store[i].id == id) s_store[i].used = false;
}

int notify_count(void)
{
    int c = 0;
    for (int i = 0; i < NOTIFY_MAX; i++) if (s_store[i].used) c++;
    return c;
}

const notif_t *notify_get(int idx)
{
    /* walk backwards from most-recently-written, skipping dismissed */
    int seen = 0;
    for (int k = 1; k <= NOTIFY_MAX; k++) {
        int i = (s_head - k + NOTIFY_MAX) % NOTIFY_MAX;
        if (s_store[i].used) {
            if (seen == idx) return &s_store[i];
            seen++;
        }
    }
    return nullptr;
}

void notify_clear_all(void)
{
    for (int i = 0; i < NOTIFY_MAX; i++) s_store[i].used = false;
    s_count = 0;
}

/* ---- tiny JSON helpers for flat Bangle.js objects ---- */

/* Copy the string value of "key" into out (unescaping the common cases).
   Returns true if found. */
static bool json_str(const char *json, const char *key, char *out, size_t outsz)
{
    char pat[32];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) { if (outsz) out[0] = 0; return false; }
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') { if (outsz) out[0] = 0; return false; }
    p++;
    size_t o = 0;
    while (*p && *p != '"' && o + 1 < outsz) {
        if (*p == '\\') {
            p++;
            switch (*p) {
                case 'n': case 'r': case 't': out[o++] = ' '; break;
                case 'u':  /* \uXXXX — skip the 4 hex (drop non-ASCII) */
                    if (p[1] && p[2] && p[3] && p[4]) p += 4;
                    break;
                case '\0': break;
                default: out[o++] = *p; break;   /* \" \\ \/ etc. */
            }
            if (*p) p++;
        } else {
            out[o++] = *p++;
        }
    }
    out[o] = 0;
    return true;
}

static int32_t json_int(const char *json, const char *key, int32_t dflt)
{
    char pat[32];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return dflt;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    return (*p == '-' || (*p >= '0' && *p <= '9')) ? (int32_t)strtol(p, nullptr, 10) : dflt;
}

/* True if a notification source is the phone/dialer app, whose call
 * notifications duplicate our richer t:"call" handling. Matched loosely because
 * Gadgetbridge reports the app's display name, which varies by phone/locale. */
static bool is_dialer_src(const char *src)
{
    if (!src || !src[0]) return false;
    char low[24];
    size_t i = 0;
    for (; src[i] && i < sizeof(low) - 1; i++)
        low[i] = (src[i] >= 'A' && src[i] <= 'Z') ? src[i] + 32 : src[i];
    low[i] = 0;
    return strcmp(low, "phone") == 0 || strstr(low, "dialer") ||
           strstr(low, "call screen") || strcmp(low, "call") == 0;
}

/* ---- protocol dispatch (called from loop, not the BLE task) ---- */
static void handle_object(const char *json)
{
    char t[24];
    if (!json_str(json, "t", t, sizeof t)) return;

    /* Do Not Disturb: ignore every incoming notification and call. Dismissals
     * (notify-) still pass through so the list stays in sync with the phone. */
    if (s_dnd && strcmp(t, "notify-") != 0) return;

    if (strcmp(t, "notify") == 0) {
        notif_t n; memset(&n, 0, sizeof n);
        n.id = json_int(json, "id", 0);
        json_str(json, "src",    n.src,   sizeof n.src);
        json_str(json, "title",  n.title, sizeof n.title);
        json_str(json, "body",   n.body,  sizeof n.body);
        if (!n.title[0]) json_str(json, "sender", n.title, sizeof n.title);
        /* Drop the dialer app's own call notification — we already surface calls
         * from the richer t:"call" event (which carries the phone number), so the
         * bare "Phone" duplicate would just clutter the list. */
        if (is_dialer_src(n.src)) return;
        store_add(&n);
        notify_alert_once();   /* buzz/chime + wake so the banner is seen */
    } else if (strcmp(t, "notify-") == 0) {   /* dismissed on phone */
        store_dismiss(json_int(json, "id", 0));
    } else if (strcmp(t, "call") == 0) {
        /* Live call state. Gadgetbridge sends cmd = incoming|outgoing|accept|
         * start|end|reject with name/number. We surface an incoming ring as a
         * store entry (so it pops a banner + lands in the list); the matching
         * end/accept/reject clears it. Needs the phone-side Phone permission. */
        char cmd[16] = {0};
        json_str(json, "cmd", cmd, sizeof cmd);
        if (strcmp(cmd, "incoming") == 0) {
            notif_t n; memset(&n, 0, sizeof n);
            n.id = CALL_NOTIF_ID;
            strncpy(n.src, "Incoming call", sizeof n.src - 1);
            json_str(json, "name",   n.title, sizeof n.title);
            json_str(json, "number", n.body,  sizeof n.body);
            if (!n.title[0]) { strncpy(n.title, n.body[0] ? n.body : "Unknown",
                                       sizeof n.title - 1); n.body[0] = 0; }
            store_dismiss(CALL_NOTIF_ID);   /* replace any stale ring */
            store_add(&n);
            call_ring_start();              /* buzz until answered/rejected/ended */
        } else {                            /* end / accept / reject / start */
            store_dismiss(CALL_NOTIF_ID);
            call_ring_stop();
        }
    }
    /* other types (music, …) handled in later milestones */
}

/* pull the JSON object out of a line that may be wrapped as GB({...}) and/or
   prefixed with a 0x10 (DLE) byte — parse between the first '{' and last '}'. */
static void handle_line(char *line)
{
    char *a = strchr(line, '{');
    char *b = strrchr(line, '}');
    if (!a || !b || b < a) return;
    b[1] = 0;
    handle_object(a);
}

/* ---- BLE callbacks ---- */
static volatile uint32_t s_rx_bytes = 0;   /* total bytes ever received from phone */

class RxCb : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        uint8_t *d = c->getData();
        size_t   n = c->getLength();
        if (!d || !n) return;
        portENTER_CRITICAL(&s_mux);
        s_rx_bytes += n;
        for (size_t i = 0; i < n && s_rx_len < sizeof(s_rx) - 1; i++)
            s_rx[s_rx_len++] = (char)d[i];
        portEXIT_CRITICAL(&s_mux);
    }
};

uint32_t notify_ble_rx_bytes(void) { return s_rx_bytes; }

static volatile uint32_t s_connects    = 0;
static volatile uint32_t s_disconnects = 0;
static volatile uint8_t  s_last_reason = 0;
/* Latest connection interval reported by the controller, in units of 1.25 ms
 * (0 = not connected). Power opt #2 verification: this should read a few-hundred
 * ms, not tens, once the longer params are honoured. */
static volatile uint16_t s_conn_interval = 0;

/* BLEDevice invokes this for EVERY GAP event in addition to its own handling
 * (setCustomGapHandler), so we can track the LIVE connection interval — it fires
 * whenever the params change, e.g. after the phone accepts our slow-down request
 * (opt #2). Without this the readout would be stuck at the connect-time value. */
static void notify_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    if (event == ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT && param)
        s_conn_interval = param->update_conn_params.conn_int;   /* 1.25 ms units */
}

class SrvCb : public BLEServerCallbacks {
    /* Use the param overloads so we can capture the disconnect reason + conn params. */
    void onConnect(BLEServer *s, esp_ble_gatts_cb_param_t *param) override {
        s_connected = true; s_connects++;
        if (param) {
            s_conn_interval = param->connect.conn_params.interval;  // 1.25 ms units
            /* Power opt #2: ask the phone for a long connection interval + slave
             * latency so the radio sleeps between events (notifications tolerate
             * latency fine). Units: interval 1.25 ms, timeout 10 ms. Request
             * 200-320 ms, latency 2, 5 s supervision timeout. */
            s->updateConnParams(param->connect.remote_bda,
                                 0xA0 /*160 =200ms*/, 0x100 /*256 =320ms*/,
                                 2 /*latency*/, 500 /*=5s*/);
        }
    }
    void onDisconnect(BLEServer *, esp_ble_gatts_cb_param_t *param) override {
        s_connected = false; s_disconnects++;
        s_conn_interval = 0;
        if (param) s_last_reason = (uint8_t)param->disconnect.reason;
        if (s_active) BLEDevice::startAdvertising();
    }
};

uint32_t notify_ble_connects(void)    { return s_connects;    }
uint32_t notify_ble_disconnects(void) { return s_disconnects; }
uint8_t  notify_ble_last_reason(void) { return s_last_reason; }
/* Connection interval in ms (0 if not connected) — opt #2 verification. */
uint16_t notify_ble_conn_interval_ms(void) { return (uint16_t)(s_conn_interval * 5 / 4); }

/* ---- public ---- */
/* User intent: once notifications have been started, keep them alive across
 * radio contention (WiFi/scanners/mouse) and rebuild automatically when free. */
static bool s_want_on = false;

/* Has the BLEDevice stack + GATT server been constructed this session? The
 * Arduino BLE library can't cleanly re-init after a full teardown, so we build
 * it ONCE and thereafter only pause/resume advertising. */
static bool s_built = false;

static void radio_reacquire(void);   /* fwd: re-enable radio after a WiFi tool */

bool notify_ble_begin(void)
{
    s_want_on = true;   /* remember the intent even if we can't start right now */
    if (s_active) return true;
    settings_load();   /* pull vibrate/sound/DND prefs off SD before RX starts */

    if (!s_built) {
        /* First time: build the whole stack. Can't build on top of a scanner
         * (it owns the GAP callback); take the mouse's radio if it has it. */
        if (ble_scan_active()) return false;
        if (mouse_hid_is_running()) mouse_hid_stop();

        /* Name MUST start with "Bangle.js" — Gadgetbridge selects its protocol by
         * name prefix, else it's "unsupported". User aliases it in the app. */
        BLEDevice::init("Bangle.js T-Watch Ultra");
        BLEDevice::setCustomGapHandler(notify_gap_cb);  /* live conn-interval (opt #2) */

        /* Raise the local MTU cap (default 23 truncated outgoing JSON to 24 bytes,
         * dropping the closing brace). Gadgetbridge negotiates up to fit a line. */
        BLEDevice::setMTU(512);

        s_server = BLEDevice::createServer();
        if (!s_server) { BLEDevice::deinit(false); return false; }
        s_server->setCallbacks(new SrvCb());

        BLEService *svc = s_server->createService(NUS_SVC);
        s_tx = svc->createCharacteristic(NUS_TX, BLECharacteristic::PROPERTY_NOTIFY);
        s_tx->addDescriptor(new BLE2902());
        BLECharacteristic *rx = svc->createCharacteristic(
            NUS_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
        rx->setCallbacks(new RxCb());

        /* Secure pairing with a displayed PIN (MITM, watch displays NOTIFY_PIN via
         * a fixed static passkey). Both encryption keys distributed or bonding
         * drops on the first report (learned from mouse HID). */
        s_paired = false; s_pair_fail = false;
        BLEDevice::setSecurityCallbacks(new SecCb());
        uint32_t passkey = NOTIFY_PIN;
        esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, &passkey, sizeof(passkey));

        BLESecurity security;
        security.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);  /* MITM => PIN */
        security.setCapability(ESP_IO_CAP_OUT);                        /* watch displays PIN */
        security.setKeySize(16);
        security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
        security.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

        svc->start();

        /* Advertising: appearance + a small 16-bit UUID + scan response with the
         * name (the 128-bit NUS UUID overflowed the advert and broke discovery).
         * Set up once; start/stop toggles it on resume/pause. */
        BLEAdvertising *adv = BLEDevice::getAdvertising();
        adv->setAppearance(0x00C0);                       /* Generic Watch icon */
        adv->addServiceUUID(BLEUUID((uint16_t)0x1811));   /* Alert Notification Service */
        adv->setScanResponse(true);

        /* Power opt #2 (assist): advertise a preferred long connection interval
         * so the phone connects slow from the start (units 1.25 ms: 200-320 ms). */
        adv->setMinPreferred(0xA0);
        adv->setMaxPreferred(0x100);

        /* Power opt #4: advertise less often while disconnected. Units 0.625 ms:
         * 0x500=1280 -> ~800 ms, 0x800=2048 -> ~1.28 s. Slower reconnect discovery
         * (~1 s) in exchange for much lower idle-advertising draw. */
        adv->setMinInterval(0x500);
        adv->setMaxInterval(0x800);

        /* Keep the controller alive across scanner sessions — the Arduino BLE lib
         * can't re-init after a teardown, so scanners must not deinit it. */
        ble_scan_keep_controller(true);
        s_built = true;
    }

    radio_reacquire();   /* re-enable bluedroid + controller if a WiFi tool freed them */
    BLEDevice::startAdvertising();
    s_active = true;
    return true;
}

/* Radio suspended (bluedroid + controller disabled) for a WiFi tool. */
static bool s_radio_suspended = false;

/* "Stop" is a PAUSE: drop the connection + advertising but keep the BLEDevice
 * stack built, so we can resume without a re-init the library can't survive.
 * Used by BLE scanners, which then SHARE our still-enabled controller. */
void notify_ble_stop(void)
{
    if (!s_active) return;
    call_ring_stop();
    if (s_built) {
        BLEDevice::stopAdvertising();
        if (s_connected && s_server) s_server->disconnect(s_server->getConnId());
    }
    s_active = false; s_connected = false;
    portENTER_CRITICAL(&s_mux); s_rx_len = 0; portEXIT_CRITICAL(&s_mux);
}

/* Deeper release for WiFi tools: WiFi can't come up cleanly alongside the heavy
 * bluedroid stack, so disable bluedroid + the BT controller to free the radio.
 * These are the REVERSIBLE enable/disable calls (not init/deinit), so the stack
 * stays built and reacquire brings it back — no fragile BLEDevice re-init. */
void notify_ble_suspend_radio(void)
{
    call_ring_stop();
    if (s_active && s_built) {
        BLEDevice::stopAdvertising();
        if (s_connected && s_server) s_server->disconnect(s_server->getConnId());
    }
    s_active = false; s_connected = false;
    if (s_built && !s_radio_suspended) {
        esp_bluedroid_disable();
        esp_bt_controller_disable();
        s_radio_suspended = true;
    }
}

/* Called from notify_ble_begin before it re-advertises. If a WiFi tool suspended
 * the radio, bring it back. Re-enabling bluedroid + the controller in-place after
 * WiFi has proven to hang with this BLE library (its stack can't cleanly resume),
 * so a clean reboot is the reliable path — it resets both radios and notifications
 * come back fresh. This only fires after a WiFi tool, never on a normal start. */
static void radio_reacquire(void)
{
    if (!s_radio_suspended) return;
    delay(60);        // let the pending screen redraw so it's not a black flash
    ESP.restart();
}

bool notify_ble_active(void)    { return s_active; }
bool notify_ble_connected(void) { return s_connected; }
bool notify_ble_is_built(void)  { return s_built; }

/* Negotiated ATT MTU with the connected phone (0 if not connected). Diagnostic:
 * outgoing control messages need MTU >= message length + 3 to fit one packet. */

/* Keep-alive: called from the main loop. A radio tool (WiFi/BLE scanner/mouse)
 * pauses the link; this resumes it automatically once the tool finishes, so the
 * user doesn't have to reopen the Notify screen to get notifications back. The
 * BLEDevice stack is kept built the whole time (ble_scan_keep_controller), so
 * resuming is just re-advertising — no fragile re-init. */
void notify_ble_keepalive(void)
{
    if (!s_want_on) return;

    static uint32_t last_ms = 0;
    uint32_t now = millis();
    if (now - last_ms < 2000) return;   /* cheap: check a few times a minute */
    last_ms = now;

    /* Stand down while another radio owner is active — don't fight WiFi or a
     * scanner for the antenna; just wait for them to finish. */
    if (ble_scan_active() || mouse_hid_is_running() || WiFi.getMode() != WIFI_MODE_NULL)
        return;

    if (!s_active) notify_ble_begin();    /* radio is free — resume advertising */
}

void notify_ble_send_line(const char *json)
{
    if (!s_tx || !s_connected) return;
    /* Terminate with CR-LF, not a bare LF. Gadgetbridge's line splitter takes
     * substring(0, indexOf('\n') - 1) — it always drops the character right
     * before the '\n', assuming a '\r' is there (Bangle.js sends via
     * Bluetooth.println(), which emits \r\n). With a bare '\n' it strips our
     * closing '}' instead, making every object "unterminated" (the char-24 error
     * on {"t":"call","n":"REJECT"}). The '\r' gives it a throwaway char to eat. */
    String s(json); s += "\r\n";
    /* Whole message in one notification — the negotiated MTU is high. */
    s_tx->setValue((uint8_t *)s.c_str(), s.length());
    s_tx->notify();
}

void notify_ble_loop(void)
{
    call_ring_tick();       /* keep the call ring pulsing (independent of RX) */
    if (!s_active) return;
    /* copy out complete lines under the lock, parse outside it */
    static char work[2048];
    size_t wlen = 0;
    portENTER_CRITICAL(&s_mux);
    /* find last newline; process everything up to it */
    size_t cut = 0;
    for (size_t i = 0; i < s_rx_len; i++) if (s_rx[i] == '\n') cut = i + 1;
    if (cut) {
        wlen = cut < sizeof(work) ? cut : sizeof(work) - 1;
        memcpy(work, s_rx, wlen);
        memmove(s_rx, s_rx + cut, s_rx_len - cut);
        s_rx_len -= cut;
    }
    portEXIT_CRITICAL(&s_mux);
    if (!wlen) return;
    work[wlen] = 0;

    char *save = nullptr;
    for (char *ln = strtok_r(work, "\n", &save); ln; ln = strtok_r(nullptr, "\n", &save))
        handle_line(ln);
}

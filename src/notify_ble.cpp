/*
 * Notification link over BLE Nordic-UART, Gadgetbridge Bangle.js protocol.
 * See notify_ble.h. Milestone 1: receive + store notifications (mirroring).
 */
#include "notify_ble.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include <string.h>

/* Secure pairing: require a bonded, MITM-protected link. The watch "displays"
 * a fixed passkey (NOTIFY_PIN) which the phone must enter — so a random device
 * can't silently connect and read your notifications. */
class SecCb : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() override { return NOTIFY_PIN; }
    void     onPassKeyNotify(uint32_t) override {}
    bool     onSecurityRequest() override { return true; }
    bool     onConfirmPIN(uint32_t) override { return true; }
    void     onAuthenticationComplete(esp_ble_auth_cmpl_t) override {}
};

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
static int     s_head  = 0;   /* index of next slot to write */
static int     s_count = 0;

static void store_add(const notif_t *n)
{
    s_store[s_head] = *n;
    s_store[s_head].used = true;
    s_head = (s_head + 1) % NOTIFY_MAX;
    if (s_count < NOTIFY_MAX) s_count++;
}

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

/* ---- protocol dispatch (called from loop, not the BLE task) ---- */
static void handle_object(const char *json)
{
    char t[24];
    if (!json_str(json, "t", t, sizeof t)) return;

    if (strcmp(t, "notify") == 0) {
        notif_t n; memset(&n, 0, sizeof n);
        n.id = json_int(json, "id", 0);
        json_str(json, "src",    n.src,   sizeof n.src);
        json_str(json, "title",  n.title, sizeof n.title);
        json_str(json, "body",   n.body,  sizeof n.body);
        if (!n.title[0]) json_str(json, "sender", n.title, sizeof n.title);
        store_add(&n);
    } else if (strcmp(t, "notify-") == 0) {   /* dismissed on phone */
        store_dismiss(json_int(json, "id", 0));
    }
    /* other types (call, music, …) handled in later milestones */
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
class RxCb : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        uint8_t *d = c->getData();
        size_t   n = c->getLength();
        if (!d || !n) return;
        portENTER_CRITICAL(&s_mux);
        for (size_t i = 0; i < n && s_rx_len < sizeof(s_rx) - 1; i++)
            s_rx[s_rx_len++] = (char)d[i];
        portEXIT_CRITICAL(&s_mux);
    }
};

static volatile uint32_t s_connects    = 0;
static volatile uint32_t s_disconnects = 0;
static volatile uint8_t  s_last_reason = 0;

class SrvCb : public BLEServerCallbacks {
    /* Use the param overloads so we can capture the disconnect reason code. */
    void onConnect(BLEServer *, esp_ble_gatts_cb_param_t *) override {
        s_connected = true; s_connects++;
    }
    void onDisconnect(BLEServer *, esp_ble_gatts_cb_param_t *param) override {
        s_connected = false; s_disconnects++;
        if (param) s_last_reason = (uint8_t)param->disconnect.reason;
        if (s_active) BLEDevice::startAdvertising();
    }
};

uint32_t notify_ble_connects(void)    { return s_connects;    }
uint32_t notify_ble_disconnects(void) { return s_disconnects; }
uint8_t  notify_ble_last_reason(void) { return s_last_reason; }

/* ---- public ---- */
void notify_ble_begin(void)
{
    if (s_active) return;
    BLEDevice::init("Bangle.js Ultra");   /* name prefix Gadgetbridge recognises */

    /* Proven "Just Works" bonded pairing — copied from the working mouse HID.
     * Without a full security config, the SMP pairing request Android/Gadgetbridge
     * sends on connect has nothing to negotiate and the link drops ("cannot
     * connect"). Both Init AND Resp encryption keys must be distributed or
     * bonding fails (see mouse_hid.cpp). No PIN yet — get the link up first;
     * upgrade to a passkey (IO_CAP_OUT + MITM) once mirroring works. */
    BLESecurity *security = new BLESecurity();
    security->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
    security->setCapability(ESP_IO_CAP_NONE);
    security->setKeySize(16);
    security->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    security->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    s_server = BLEDevice::createServer();
    s_server->setCallbacks(new SrvCb());

    BLEService *svc = s_server->createService(NUS_SVC);
    s_tx = svc->createCharacteristic(NUS_TX, BLECharacteristic::PROPERTY_NOTIFY);
    s_tx->addDescriptor(new BLE2902());
    BLECharacteristic *rx = svc->createCharacteristic(
        NUS_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    rx->setCallbacks(new RxCb());
    svc->start();

    /* Use the DEFAULT advertising path — exactly like the working mouse HID.
     * The custom setAdvertisementData path skips the library's sequenced GAP
     * config and can start advertising before the data is applied, which breaks
     * connectability. With setScanResponse(true) the device name goes into the
     * scan response (where it fits); we deliberately do NOT advertise the
     * 128-bit NUS UUID (it would overflow) — Gadgetbridge finds us by name. */
    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->setAppearance(0x00C0);        /* Generic Watch icon */
    adv->setScanResponse(true);        /* name -> scan response */
    adv->start();
    s_active = true;
}

void notify_ble_stop(void)
{
    if (!s_active) return;
    BLEDevice::deinit(false);
    s_server = nullptr; s_tx = nullptr;
    s_active = false; s_connected = false;
    portENTER_CRITICAL(&s_mux); s_rx_len = 0; portEXIT_CRITICAL(&s_mux);
}

bool notify_ble_active(void)    { return s_active; }
bool notify_ble_connected(void) { return s_connected; }

void notify_ble_send_line(const char *json)
{
    if (!s_tx || !s_connected) return;
    String s(json); s += "\n";
    s_tx->setValue((uint8_t *)s.c_str(), s.length());
    s_tx->notify();
}

void notify_ble_loop(void)
{
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

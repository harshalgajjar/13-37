#include "mouse_hid.h"
#include "ble_scan_manager.h"
#include "notify_ble.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>

// HID report descriptor for a 3-button mouse with a scroll wheel.
// Input report (ID 1) is 4 bytes: [buttons, dx, dy, wheel].
static const uint8_t HID_REPORT_MAP[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (Button 1)
    0x29, 0x03,        //     Usage Maximum (Button 3)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x03,        //     Report Count (3)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data,Var,Abs)  ; 3 button bits
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x05,        //     Report Size (5)
    0x81, 0x03,        //     Input (Const,Var,Abs) ; 5-bit padding
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x03,        //     Report Count (3)
    0x81, 0x06,        //     Input (Data,Var,Rel) ; X, Y, Wheel
    0xC0,              //   End Collection
    0xC0,              // End Collection
};

static bool               s_running   = false;
static volatile bool      s_connected = false;
static BLEServer         *s_server    = nullptr;
static BLEHIDDevice      *s_hid       = nullptr;
static BLECharacteristic *s_input     = nullptr;

class MouseServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *) override { s_connected = true; }
    void onDisconnect(BLEServer *) override {
        s_connected = false;
        // Re-advertise so the host (or a different one) can reconnect.
        if (s_running) BLEDevice::startAdvertising();
    }
};
static MouseServerCallbacks s_server_cbs;

static int8_t clamp8(int v)
{
    if (v >  127) return  127;
    if (v < -127) return -127;
    return (int8_t)v;
}

static void send_report(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel)
{
    if (!s_input || !s_connected) return;
    uint8_t report[4] = { buttons, (uint8_t)dx, (uint8_t)dy, (uint8_t)wheel };
    s_input->setValue(report, sizeof(report));
    s_input->notify();
}

bool mouse_hid_start()
{
    if (s_running) return true;

    // The BLE controller exposes a single GAP callback slot; it can't be shared
    // between this HID peripheral and the scan manager. Refuse if a scanner
    // (AirTag / wardriver) currently holds it.
    if (ble_scan_active()) return false;

    // The notification link owns BLEDevice for the whole session (the Arduino BLE
    // library can't re-init after a teardown), so the mouse — which needs its own
    // BLEDevice with a different name + HID service — can't run alongside it.
    // Refuse rather than corrupt the shared stack. (Notifications are the primary
    // BLE feature; the mouse is available only when notifications were never
    // started this boot.)
    if (notify_ble_is_built()) return false;

    BLEDevice::init("T-Watch Mouse");

    s_server = BLEDevice::createServer();
    if (!s_server) {                  // BLE stack failed to come up
        BLEDevice::deinit(false);
        return false;
    }
    s_server->setCallbacks(&s_server_cbs);

    s_hid = new BLEHIDDevice(s_server);
    if (!s_hid) {
        BLEDevice::deinit(false);
        s_server = nullptr;
        return false;
    }

    s_input = s_hid->inputReport(1);   // report ID 1
    if (!s_input) {
        BLEDevice::deinit(false);
        s_server = nullptr;
        s_hid    = nullptr;
        return false;
    }

    // manufacturer() with no argument is the *creating* overload: it builds the
    // (optional) manufacturer characteristic and returns it. The constructor
    // does NOT create it, so the value must be set on this returned pointer —
    // calling the void manufacturer(name) overload directly dereferences an
    // uninitialised pointer and crashes (LoadProhibited).
    BLECharacteristic *mfr = s_hid->manufacturer();
    if (mfr) mfr->setValue(std::string("LilyGo"));

    s_hid->pnp(0x02, 0x303A, 0x1812, 0x0001);  // sig=USB-IF, vid, pid, version
    s_hid->hidInfo(0x00, 0x02);                // country 0, remote-wake flag

    // "Just Works" bonded pairing — no PIN. BLEHIDDevice marks the HID input
    // report (and its CCCD) as encryption-required, so the host must complete
    // bonding before it accepts any report. Distributing the encryption +
    // identity keys is what makes bonding actually succeed — without
    // setInit/RespEncryptionKey the link drops on the first report.
    BLESecurity security;
    security.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
    security.setCapability(ESP_IO_CAP_NONE);
    security.setKeySize(16);
    security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    security.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    s_hid->reportMap((uint8_t *)HID_REPORT_MAP, sizeof(HID_REPORT_MAP));
    s_hid->startServices();
    s_hid->setBatteryLevel(100);

    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->setAppearance(HID_MOUSE);
    adv->addServiceUUID(s_hid->hidService()->getUUID());
    adv->setScanResponse(true);
    adv->start();

    s_running = true;
    return true;
}

void mouse_hid_stop()
{
    if (!s_running) return;
    s_running   = false;
    s_connected = false;

    // deinit(false): tear down the stack but keep controller memory, so the BLE
    // scanners (or a later mouse restart) can bring BLE back up. Note: this BLE
    // library has no free path for the BLEServer/BLEHIDDevice objects, so each
    // start/stop cycle leaks a few KB — fine for occasional use.
    BLEDevice::deinit(false);
    s_server = nullptr;
    s_hid    = nullptr;
    s_input  = nullptr;
}

bool mouse_hid_is_running()   { return s_running;   }
bool mouse_hid_is_connected() { return s_connected; }

void mouse_hid_move(int dx, int dy)
{
    send_report(0, clamp8(dx), clamp8(dy), 0);
}

void mouse_hid_scroll(int wheel)
{
    send_report(0, 0, 0, clamp8(wheel));
}

void mouse_hid_click(uint8_t buttons)
{
    send_report(buttons, 0, 0, 0);
    delay(20);
    send_report(0, 0, 0, 0);
}

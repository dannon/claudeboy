#include "device/xport.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <atomic>
#include <string.h>

#include "core/reassemble.h"
#include "esp_heap_caps.h"

namespace cbxport {
namespace {

// Fixed for the life of the project: the Swift helper scans for the service
// UUID, so these strings are the contract between the two halves.
const char* SERVICE_UUID  = "36979b4a-6f15-447a-a4ae-0dd4ecf0967b";
const char* SNAPSHOT_UUID = "678c5468-ad9d-402f-a06e-267c729245f3";

// The reassembly buffer is ours, not the caller's: writes arrive on the NimBLE
// host task at whatever moment the Mac chooses, and main.cpp's g_body is being
// parsed on the Arduino loop. take_snapshot() copies across the boundary.
//
// Sized to match main.cpp's g_body (also 6144) -- nothing in the build enforces
// that the two agree, so the n == 0 || n > cap check below is defence, not dead
// code: if they ever diverge, a smaller g_rx makes the reassembler hand back
// TooLarge on the oversized START, and a larger g_rx trips that check instead.
// Either way the mismatch fails gracefully rather than overrunning a buffer.
char g_rx[6144];
cb::Reassembler g_re;

// Written on the BLE host task, read on the Arduino loop -- and on this chip
// those are different cores. `volatile` stops the compiler reordering these but
// is not a memory barrier, so it would not guarantee the loop sees the bytes of
// g_rx that the callback wrote before it set the flag. std::atomic with
// release/acquire does, and at one payload a minute the cost is unmeasurable.
//
// The handoff invariant, which take_snapshot() depends on: the callback refuses
// every frame while g_ready is set, and the loop clears g_ready only AFTER it
// has finished copying out of g_rx. So the buffer is never written while it is
// being read, without either side taking a lock.
std::atomic<bool> g_ready{false};
size_t g_ready_len = 0;   // published by the release store below; read after the acquire load

// Written on the BLE host task, read on the Arduino loop, same cross-core split
// as g_ready above -- so this gets the same std::atomic treatment for the same
// reason a plain bool would not do. Unlike g_ready it orders nothing else: it
// is one aligned byte with a single writer, consumed only to render the status
// string, so relaxed is the right memory order on both the load and the store.
std::atomic<bool> g_connected{false};

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* s) override {
        g_connected.store(true, std::memory_order_relaxed);
        Serial.println("claudeboy: ble central connected");
        // Nothing to do here: the NimBLE controller stops connectable
        // advertising on connection by itself. One central is the whole
        // design, and a second would have nothing to say anyway.
    }
    void onDisconnect(NimBLEServer* s) override {
        g_connected.store(false, std::memory_order_relaxed);
        cb::reassemble_init(g_re, g_rx, sizeof g_rx);   // drop any partial transfer
        Serial.println("claudeboy: ble central disconnected, advertising again");
        // Belt and braces, not load-bearing: NimBLEServer::m_advertiseOnDisconnect
        // defaults true, and NimBLEServer calls startAdvertising() itself right
        // after this callback returns. This call is a no-op today, kept in case
        // that default ever changes under us.
        s->startAdvertising();
    }
};

class SnapshotCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        // The loop has not consumed the last payload yet. Dropping this frame is
        // correct rather than merely convenient: the Mac pushes on change and
        // will send the next one whole, and overwriting g_rx here would tear a
        // buffer the loop is copying out of.
        if (g_ready.load(std::memory_order_acquire)) return;

        const std::string v = c->getValue();
        size_t len = 0;
        const cb::XferResult r = cb::reassemble_frame(
            g_re, (const uint8_t*)v.data(), v.size(), len);

        switch (r) {
            case cb::XferResult::Complete:
                g_ready_len = len;
                g_ready.store(true, std::memory_order_release);
                break;
            case cb::XferResult::NeedMore:
                break;
            case cb::XferResult::NoTransfer:
                // Expected, not anomalous: this is what every DATA frame of a
                // transfer looks like once its START has been dropped by the
                // g_ready guard above. Logging it would mean a blocking
                // Serial.printf per frame -- ~20 of them at a negotiated MTU,
                // closer to 185 without one -- from the NimBLE host task while
                // it should be servicing the link. Stay quiet.
                break;
            default:
                Serial.printf("claudeboy: ble frame refused, result=%d, %u bytes\n",
                              (int)r, (unsigned)v.size());
                break;
        }
    }
};

ServerCallbacks   g_server_cb;
SnapshotCallbacks g_char_cb;

}  // namespace

void begin() {
    cb::reassemble_init(g_re, g_rx, sizeof g_rx);

    NimBLEDevice::init("ClaudeBoy");
    // The default is 3dBm. This board sits an arm's length from the Mac, and the
    // last transport failed for want of link margin, so take the maximum.
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    // Just-works bonding: no display and no keyboard on this board, so there is
    // no PIN to compare and MITM protection is not on offer. What it does buy is
    // an encrypted link bound to one central -- which is what lets the read token
    // stay out of this firmware entirely. The bond lives in NVS, so erase_flash
    // means pairing again.
    NimBLEDevice::setSecurityAuth(true, false, true);   // bond, no MITM, secure connections
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks(&g_server_cb);

    NimBLEService* service = server->createService(SERVICE_UUID);
    // The helper writes with response, and the Swift side is written that way
    // deliberately -- but not because this property would refuse anything else.
    // It would not: NimBLE merges WRITE and WRITE_NO_RSP into a single ATT
    // permission bit (ble_gatts.c), the permission check never looks at which
    // opcode arrived (ble_att_svr.c), and onWrite() fires either way. What
    // with-response actually buys is pacing. A Write Request is confirmed and
    // one in flight, so the twenty-odd frames of a payload arrive in order and
    // one at a time, which is what the reassembler assumes. Write Commands can
    // be dropped under controller buffer pressure, and a dropped DATA frame
    // means the transfer never reaches its declared length -- not corruption,
    // but a payload lost until the next START resets things.
    NimBLECharacteristic* snap = service->createCharacteristic(
        SNAPSHOT_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC);
    snap->setCallbacks(&g_char_cb);
    service->start();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(SERVICE_UUID);
    adv->setScanResponse(true);
    adv->start();

    Serial.printf("claudeboy: ble advertising as ClaudeBoy, free8 %u largest %u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

// Nothing to drive: NimBLE runs its own host task and the write callback does
// the work. The function exists so main.cpp does not need to know that.
void poll() {}

bool take_snapshot(char* buf, size_t cap, size_t& len) {
    len = 0;
    if (!g_ready.load(std::memory_order_acquire)) return false;

    const size_t n = g_ready_len;
    if (n == 0 || n > cap) {
        Serial.printf("claudeboy: ble payload of %u bytes does not fit %u\n",
                      (unsigned)n, (unsigned)cap);
        g_ready.store(false, std::memory_order_release);
        return false;
    }
    memcpy(buf, g_rx, n);
    len = n;
    // Last, and deliberately: the callback refuses frames while this is set, so
    // clearing it only now is what keeps g_rx still while it is being copied.
    g_ready.store(false, std::memory_order_release);
    return true;
}

const char* status_text() {
    return g_connected.load(std::memory_order_relaxed) ? "BLE LINKED" : "BLE ADVERTISING";
}

}  // namespace cbxport

#include "ble_server.h"
#include "../config.h"
#include <NimBLEDevice.h>
#include <Arduino.h>

// Forward decl at file scope (NOT in namespace) — defined in main.cpp.
// Lets BleServer::sendFrame() report progress to the Pi via JSON over USB.
extern void sendUsbLog(const char* msg);

static NimBLEServer*          gServer    = nullptr;
static NimBLECharacteristic*  gTxChar    = nullptr;  // ESP32→Android (Notify)
static NimBLECharacteristic*  gRxChar    = nullptr;  // Android→ESP32 (Write)

static OnFrameReceived        gOnFrame;
static OnClientConnected      gOnConnected;
static OnClientDisconnected   gOnDisconnected;

static std::vector<uint8_t>   gRxBuf;
static bool                   gSubscribeNotified = false;  // reset on each BLE connection

// ─── Server callbacks (Open BLE — no bonding, no encryption) ─────────────────

class HaServerCallbacks : public NimBLEServerCallbacks {
public:
    void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
        server->setDataLen(info.getConnHandle(), 251);
        gSubscribeNotified = false;
        // gOnConnected fires from HaTxCallbacks::onSubscribe (after Android writes CCCD).
    }

    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& info, int reason) override {
        gRxBuf.clear();
        if (gOnDisconnected) gOnDisconnected();
        NimBLEDevice::startAdvertising();
    }
};

// ─── TX Characteristic callbacks (subscribe = Android ready to receive) ───────

class HaTxCallbacks : public NimBLECharacteristicCallbacks {
public:
    void onSubscribe(NimBLECharacteristic* pChar, NimBLEConnInfo& info, uint16_t subValue) override {
        // subValue: 0 = unsubscribed, 1 = notifications, 2 = indications
        // Guard with gSubscribeNotified so Android CCCD retries never send a duplicate signal.
        if (subValue == 1 && !gSubscribeNotified && gOnConnected) {
            gSubscribeNotified = true;
            gOnConnected();
        }
    }
};

// ─── RX Characteristic callbacks (Android writes to ESP32) ───────────────────

class HaRxCallbacks : public NimBLECharacteristicCallbacks {
public:
    void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& info) override {
        const uint8_t* data = pChar->getValue().data();
        size_t len = pChar->getValue().size();
        if (len < 1) return;

        uint8_t flag = data[0];
        gRxBuf.insert(gRxBuf.end(), data + 1, data + len);

        if (flag == BLE_CHUNK_FINAL) {
            if (gOnFrame && !gRxBuf.empty()) {
                gOnFrame(gRxBuf.data(), gRxBuf.size());
            }
            gRxBuf.clear();
        }
    }
};

// ─── Public API ──────────────────────────────────────────────────────────────

namespace BleServer {

void begin(OnFrameReceived onFrame,
           OnClientConnected onConnected,
           OnClientDisconnected onDisconnected)
{
    gOnFrame        = onFrame;
    gOnConnected    = onConnected;
    gOnDisconnected = onDisconnected;

    NimBLEDevice::init(HA_DEVICE_NAME);
    // Clear any bond keys left from a previous firmware that used BLE Secure
    // Connections. With Open BLE (no bonding) these would cause NimBLE to send
    // an unexpected security request on reconnection, crashing the stack.
    NimBLEDevice::deleteAllBonds();
    NimBLEDevice::setPower(3);  // +3 dBm
    // Open BLE: no bonding, no encryption — security is at the application layer
    // via the shared passcode embedded in every packet.
    NimBLEDevice::setSecurityAuth(false, false, false);

    gServer = NimBLEDevice::createServer();
    gServer->setCallbacks(new HaServerCallbacks());

    NimBLEService* svc = gServer->createService(HA_SERVICE_UUID);

    gTxChar = svc->createCharacteristic(HA_TX_UUID, NIMBLE_PROPERTY::NOTIFY);
    gTxChar->setCallbacks(new HaTxCallbacks());

    gRxChar = svc->createCharacteristic(
        HA_RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    gRxChar->setCallbacks(new HaRxCallbacks());

    gServer->start();

    // Standard GATT Device Information Service (0x180A)
    NimBLEService* dis = gServer->createService("180A");
    dis->createCharacteristic("2A29", NIMBLE_PROPERTY::READ)->setValue(HA_GATT_MANUFACTURER);
    dis->createCharacteristic("2A24", NIMBLE_PROPERTY::READ)->setValue(HA_GATT_MODEL);
    dis->createCharacteristic("2A27", NIMBLE_PROPERTY::READ)->setValue(HA_GATT_HW_REVISION);
    dis->createCharacteristic("2A26", NIMBLE_PROPERTY::READ)->setValue(HA_FIRMWARE_VERSION);
    // NimBLE v2.x: services start automatically with the server, no explicit call needed

    uint8_t mfr[4] = {
        (uint8_t)(HA_MANUFACTURER_ID & 0xFF),
        (uint8_t)(HA_MANUFACTURER_ID >> 8),
        HA_MANUFACTURER_DATA_H,
        HA_MANUFACTURER_DATA_A
    };

    // Primary ADV packet: Service UUID + Manufacturer Data + AD 0x08 Shortened Local Name
    NimBLEAdvertisementData advData;
    advData.setCompleteServices(NimBLEUUID(HA_SERVICE_UUID));
    advData.setManufacturerData(std::string((char*)mfr, sizeof(mfr)));
    advData.setShortName(HA_DEVICE_SHORT_NAME);  // AD 0x08 — visible without active scan

    // Scan Response packet: AD 0x09 Complete Local Name
    NimBLEAdvertisementData scanResp;
    scanResp.setName(HA_DEVICE_NAME);  // AD 0x09 — returned on active scan request

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAdvertisementData(advData);
    adv->setScanResponseData(scanResp);

    NimBLEDevice::startAdvertising();
}

bool sendFrame(const uint8_t* data, size_t len)
{
    if (!gServer || !gServer->getConnectedCount()) return false;

    size_t offset = 0;
    int chunkIdx = 0;
    while (offset < len) {
        size_t chunkLen = min((size_t)BLE_MAX_CHUNK, len - offset);
        bool   isLast   = (offset + chunkLen >= len);

        std::string chunk(1 + chunkLen, '\0');
        chunk[0] = isLast ? BLE_CHUNK_FINAL : BLE_CHUNK_MORE;
        memcpy(&chunk[1], data + offset, chunkLen);

        gTxChar->setValue(chunk);

        // Retry if NimBLE MSYS buffer pool is momentarily exhausted.
        bool sent = false;
        for (int retry = 0; retry < 10 && !sent; retry++) {
            sent = gTxChar->notify();
            if (!sent) delay(50);
        }

        if (!sent) {
            // Only log on real failure — verbose debug spam on the data channel
            // can itself starve the BLE link.
            char dbg[80];
            snprintf(dbg, sizeof(dbg), "notify FAILED at chunk %d/%u",
                     chunkIdx, (unsigned)((len + BLE_MAX_CHUNK - 1) / BLE_MAX_CHUNK));
            sendUsbLog(dbg);
            return false;
        }

        offset += chunkLen;
        chunkIdx++;
        if (!isLast) delay(20);
    }
    return true;
}

bool isConnected()
{
    return gServer && gServer->getConnectedCount() > 0;
}

void shutdown()
{
    NimBLEDevice::deinit(true);  // true = also clear any stored bond keys (none expected)
    gServer = nullptr;
    gTxChar = nullptr;
    gRxChar = nullptr;
}

void clearBonds()
{
    NimBLEDevice::deleteAllBonds();
}

void restartAdvertising()
{
    NimBLEDevice::stopAdvertising();
    NimBLEDevice::startAdvertising();
}

} // namespace BleServer

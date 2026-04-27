#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include "ble/ble_server.h"
#include "bridge/usb_bridge.h"
#include "config.h"

static volatile bool gOtaPending              = false;
static bool          gOtaActive               = false;
static volatile bool gClientConnectedPending   = false;
static volatile bool gClientDisconnectedPending = false;

// ─── OTA ─────────────────────────────────────────────────────────────────────

static void startOtaMode() {
    // NimBLE and WiFi share the ESP32-S3 RF — starting softAP while BLE is
    // active causes RF coexistence conflicts that silently prevent the AP from
    // starting (and may trigger a watchdog reset). Shut BLE down cleanly first.
    BleServer::shutdown();
    delay(200);

    // Explicitly set AP mode so the WiFi driver initialises even if it was
    // never started before (first OTA after boot with BLE-only operation).
    WiFi.mode(WIFI_AP);

    IPAddress apIP(192, 168, 200, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    if (!WiFi.softAP(OTA_AP_SSID)) {
        const char* err = "{\"type\":\"ota_error\",\"message\":\"WiFi AP start failed\"}";
        UsbBridge::sendFrame((const uint8_t*)err, strlen(err));
        return;
    }
    delay(200);

    char ready[128];
    snprintf(ready, sizeof(ready),
        "{\"type\":\"ota_ready\",\"ssid\":\"%s\",\"ip\":\"%s\"}",
        OTA_AP_SSID, WiFi.softAPIP().toString().c_str());
    UsbBridge::sendFrame((const uint8_t*)ready, strlen(ready));

    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.begin();

    gOtaActive = true;
}

// ─── USB log helper (JSON over USB-CDC, parsed by Pi) ────────────────────────

void sendUsbLog(const char* msg) {
    char buf[160];
    int n = snprintf(buf, sizeof(buf), "{\"type\":\"log\",\"msg\":\"%s\"}", msg);
    if (n > 0 && n < (int)sizeof(buf)) {
        UsbBridge::sendFrame((const uint8_t*)buf, (size_t)n);
    }
}

// ─── USB frame handler ────────────────────────────────────────────────────────

static void onUsbFrame(const uint8_t* data, size_t len) {
    // Check for control messages (max 128 bytes, quick scan)
    if (len >= 16 && len <= 128) {
        char buf[129];
        memcpy(buf, data, len);
        buf[len] = '\0';
        if (strstr(buf, "\"ota_enable\"")) {
            gOtaPending = true;
            return;
        }
        if (strstr(buf, "\"clear_bonds\"")) {
            BleServer::clearBonds();
            const char* ok = "{\"type\":\"bonds_cleared\"}";
            UsbBridge::sendFrame((const uint8_t*)ok, strlen(ok));
            return;
        }
        if (strstr(buf, "\"restart_adv\"")) {
            BleServer::restartAdvertising();
            return;
        }
    }

    if (!BleServer::isConnected()) return;
    BleServer::sendFrame(data, len);
}

// ─── BLE frame → USB ─────────────────────────────────────────────────────────

static void onBleFrame(const uint8_t* data, size_t len) {
    UsbBridge::sendFrame(data, len);
}

static void onBleConnected() {
    gClientConnectedPending = true;
}

static void onBleDisconnected() {
    gClientDisconnectedPending = true;
}

// ─── Arduino lifecycle ────────────────────────────────────────────────────────

void setup() {
    // Enlarge USB-CDC RX buffer BEFORE Serial.begin() so it actually takes effect.
    // Default 256 B overflows when the Pi pushes large ANS_DEVICES frames (~30 KB);
    // BleServer::sendFrame() takes 2-3 s to drain via BLE notifies, during which
    // the kernel cannot deliver more bytes to us. 32 KB fits the whole frame.
    Serial.setRxBufferSize(32768);
    Serial.begin(USB_SERIAL_BAUD);
    for (uint32_t t = millis(); !Serial && (millis() - t) < 2000;) delay(10);
    Serial.setTxTimeoutMs(0);

    // No Serial.print before/during UsbBridge::begin — Serial is the USB-CDC data
    // channel; any plain text would be parsed as a 4-byte frame length on the Pi
    // side and corrupt the framing.
    UsbBridge::begin(onUsbFrame);
    BleServer::begin(onBleFrame, onBleConnected, onBleDisconnected);
}

void loop() {
    // Handle BLE connect/disconnect notifications (set in callbacks, sent here on main task)
    if (gClientConnectedPending) {
        gClientConnectedPending = false;
        const char* msg = "{\"type\":\"client_connected\"}";
        UsbBridge::sendFrame((const uint8_t*)msg, strlen(msg));
    }
    if (gClientDisconnectedPending) {
        gClientDisconnectedPending = false;
        const char* msg = "{\"type\":\"client_disconnected\"}";
        UsbBridge::sendFrame((const uint8_t*)msg, strlen(msg));
    }

    UsbBridge::poll();

    if (gOtaPending && !gOtaActive) {
        gOtaPending = false;
        startOtaMode();
    }

    if (gOtaActive) {
        ArduinoOTA.handle();
    }

    delay(1);
}

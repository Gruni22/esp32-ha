#pragma once
#include <functional>
#include <Arduino.h>

// Called by ble_server when a complete reassembled frame arrives from Android
using OnFrameReceived = std::function<void(const uint8_t* data, size_t len)>;

// Called by ble_server when client connects/disconnects
using OnClientConnected    = std::function<void()>;
using OnClientDisconnected = std::function<void()>;

namespace BleServer {
    void begin(OnFrameReceived onFrame,
               OnClientConnected onConnected,
               OnClientDisconnected onDisconnected);

    // Send a complete frame to Android — chunked into BLE_MAX_CHUNK pieces
    // Returns false if no client connected
    bool sendFrame(const uint8_t* data, size_t len);

    bool isConnected();

    // Disconnect all BLE clients, stop advertising, and deinit NimBLE.
    // Call before starting WiFi softAP to avoid RF coexistence conflicts.
    void shutdown();

    // Erase all stored bond keys from NVS.
    void clearBonds();

    // Stop and restart BLE advertising. Call when the USB host reconnects to
    // ensure Android can find the device even after rapid connect/disconnect cycles.
    void restartAdvertising();
}

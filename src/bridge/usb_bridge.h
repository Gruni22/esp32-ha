#pragma once
#include <functional>
#include <stdint.h>

// Called when a complete 4-byte-framed message arrives from Pi via USB-Serial
using OnUsbFrameReceived = std::function<void(const uint8_t* data, size_t len)>;

namespace UsbBridge {
    void begin(OnUsbFrameReceived onFrame);

    // Read from USB-Serial and call onFrame for each complete frame
    // Call this frequently from loop()
    void poll();

    // Send a 4-byte-framed message to Pi via USB-Serial
    void sendFrame(const uint8_t* data, size_t len);
}

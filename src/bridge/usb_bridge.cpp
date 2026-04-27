#include "usb_bridge.h"
#include "../config.h"
#include <Arduino.h>
#include <vector>

static OnUsbFrameReceived gOnFrame;

enum class RxState { HEADER, BODY };
static RxState       gRxState   = RxState::HEADER;
static uint8_t       gHeader[4];
static int           gHeaderPos = 0;
static uint32_t      gBodyLen   = 0;
static std::vector<uint8_t> gBody;

namespace UsbBridge {

void begin(OnUsbFrameReceived onFrame) {
    gOnFrame = onFrame;
}

void poll() {
    while (Serial.available()) {
        uint8_t b = Serial.read();

        if (gRxState == RxState::HEADER) {
            gHeader[gHeaderPos++] = b;
            if (gHeaderPos == 4) {
                gBodyLen = ((uint32_t)gHeader[0] << 24) |
                           ((uint32_t)gHeader[1] << 16) |
                           ((uint32_t)gHeader[2] << 8)  |
                            (uint32_t)gHeader[3];

                if (gBodyLen == 0 || gBodyLen > USB_MAX_FRAME) {
                    // Garbage byte — slide window by 1 instead of resetting all 4
                    gHeader[0] = gHeader[1];
                    gHeader[1] = gHeader[2];
                    gHeader[2] = gHeader[3];
                    gHeaderPos = 3;
                    gBodyLen   = 0;
                } else {
                    gBody.clear();
                    gBody.reserve(gBodyLen);
                    gRxState = RxState::BODY;
                }
            }
        } else {
            gBody.push_back(b);
            if (gBody.size() >= gBodyLen) {
                if (gOnFrame) {
                    gOnFrame(gBody.data(), gBody.size());
                }
                gRxState   = RxState::HEADER;
                gHeaderPos = 0;
                gBodyLen   = 0;
                gBody.clear();
            }
        }
    }
}

void sendFrame(const uint8_t* data, size_t len) {
    uint8_t header[4] = {
        (uint8_t)(len >> 24),
        (uint8_t)(len >> 16),
        (uint8_t)(len >> 8),
        (uint8_t)(len)
    };
    Serial.write(header, 4);
    Serial.write(data, len);
    Serial.flush();
}

} // namespace UsbBridge

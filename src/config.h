#pragma once

// BLE Service and Characteristic UUIDs — identical to existing ha-bluetooth BLE protocol
#define HA_SERVICE_UUID  "a10d4b1c-bf45-4c2a-9c32-4a8f7e3d1234"
#define HA_TX_UUID       "a10d4b1c-bf45-4c2a-9c32-4a8f7e3d1235"  // ESP32→Android (Notify)
#define HA_RX_UUID       "a10d4b1c-bf45-4c2a-9c32-4a8f7e3d1236"  // Android→ESP32 (Write)

// Advertising
#define HA_DEVICE_NAME        "Homeassistant_Home"  // AD 0x09 Complete Local Name (Scan Response)
#define HA_DEVICE_SHORT_NAME  "HA Home"              // AD 0x08 Shortened Local Name (primary ADV)
// Manufacturer data: company_id=0x0002, then 'H','A' to identify as HA device
#define HA_MANUFACTURER_ID  0x0002
#define HA_MANUFACTURER_DATA_H  'H'
#define HA_MANUFACTURER_DATA_A  'A'

// GATT Device Information Service attributes
#define HA_GATT_MANUFACTURER  "Home Assistant"
#define HA_GATT_MODEL         "ESP32-HA-Gateway"
#define HA_GATT_HW_REVISION   "ESP32-S3"
#define HA_FIRMWARE_VERSION   "1.1.0"

// USB Serial (to Pi)
#define USB_SERIAL_BAUD     115200

// OTA — WiFi AP created when ota_enable command received from Pi
#define OTA_AP_SSID          "ESP32-HA-OTA"
#define OTA_HOSTNAME         "ha-gateway"

// BLE chunking — must match Android BleGattTransport
#define BLE_CHUNK_MORE   0x00
#define BLE_CHUNK_FINAL  0x01
#define BLE_MAX_CHUNK    244   // BLE MTU 247 - 3 bytes ATT overhead

// USB framing — 4-byte big-endian length prefix, identical to RFCOMM protocol
// Frame: [uint32 BE length][JSON payload UTF-8]
#define USB_MAX_FRAME    (64 * 1024)  // 64 KB max frame

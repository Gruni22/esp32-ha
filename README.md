# esp32-ha — BLE Gateway Firmware for Home Assistant

[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32--S3-orange)](https://platformio.org/)
[![Framework](https://img.shields.io/badge/Framework-Arduino-00979D)](https://www.arduino.cc/)
[![BLE Stack](https://img.shields.io/badge/BLE-NimBLE--Arduino-blue)](https://github.com/h2zero/NimBLE-Arduino)
[![License](https://img.shields.io/github/license/Gruni22/esp32-ha)](LICENSE)

Open-source firmware that turns an **ESP32-S3** into a transparent BLE gateway for the [Home Assistant Bluetooth API](https://github.com/Gruni22/ha-bluetooth) integration. The ESP32 advertises an Open-BLE GATT service over the air and tunnels every frame straight to Home Assistant over USB-CDC — no Wi-Fi, no MQTT, no cloud.

> **Companion HA integration:** [`ha-bluetooth`](https://github.com/Gruni22/ha-bluetooth)
> **Companion Android app:** [`btdashboard`](https://github.com/Gruni22/ha-android)

---

## Why an ESP32 in the loop?

Most Linux-based HA hosts (HAOS, Pi, NUC) ship Bluetooth stacks that are unsuitable for hosting a long-lived custom GATT service alongside HA's own Bluetooth integration: BlueZ contention, slow advertising, intermittent connection drops. A dedicated ESP32-S3 sidesteps all of that:

- Dedicated 2.4 GHz radio and BLE controller
- NimBLE host stack — small, fast, deterministic
- USB-CDC at 921 kbaud is more than enough for the protocol's traffic
- Cheap (~5 €) and replaceable

---

## Hardware

| Item | Notes |
|------|-------|
| **Board** | ESP32-S3 with native USB. Default target: `esp32-s3-fh4r2` (Waveshare ESP32-S3-Zero, 4 MB Flash + 2 MB PSRAM). |
| **Cable** | A short, **shielded** USB data cable. Long thin charger cables tend to drop USB-CDC bytes. |
| **Antenna** | Whichever antenna ships with the board — range is ~10 m with the chip antenna. |

Other ESP32-S3 variants (e.g. ESP32-S3-DevKitC) work too — just add a matching `[env:…]` block to `platformio.ini`.

---

## Build & flash

### Prerequisites

- [PlatformIO Core](https://platformio.org/install/cli) (or the VS Code extension)
- USB-A or USB-C cable to the ESP32-S3

### First flash (USB)

```bash
git clone https://github.com/Gruni22/esp32-ha.git
cd esp32-ha
pio run -e esp32-s3 --target upload
```

The firmware boots within ~1 s; the device starts advertising as **`Homeassistant_Home`** immediately.

### OTA flash (over Wi-Fi)

Once the firmware is running you can re-flash without a cable:

1. In Home Assistant: **Settings → Devices & Services → Bluetooth API → "Enable OTA"** button.
2. The ESP32 brings up a Wi-Fi access point named `ESP32-HA-OTA` (open, no password).
3. Connect your computer to that AP.
4. Run:

   ```bash
   pio run -e esp32-s3-ota --target upload
   ```

5. After upload the ESP32 reboots and tears down the AP. Reconnect your computer to your normal Wi-Fi.

> ⚠️ While OTA mode is active, BLE is shut down (RF coexistence). The HA integration shows the gateway as disconnected until the upload finishes.

---

## Architecture

```
┌──────────────────────────┐                       ┌──────────────────────────┐
│  Android phone /         │      Open BLE         │  ESP32-S3                │
│  Android Auto head unit  │◀════════════════════▶ │  ┌────────────────────┐  │
└──────────────────────────┘   GATT chunked        │  │ NimBLE GATT Server │  │
                               (244 B + 1 flag)    │  │  TX (Notify)       │  │
                                                   │  │  RX (Write)        │  │
                                                   │  └─────────┬──────────┘  │
                                                   │            │             │
                                                   │  ┌─────────▼──────────┐  │
                                                   │  │ USB-CDC framing    │  │
                                                   │  │ [u32 len][payload] │  │
                                                   │  └─────────┬──────────┘  │
                                                   └────────────┼─────────────┘
                                                                │
                                                                ▼
                                                       Home Assistant host
                                                       (custom_components/bluetooth_api)
```

**Source layout:**

| Path | Purpose |
|------|---------|
| `src/main.cpp` | Arduino lifecycle, glue between USB and BLE callbacks, OTA bootstrap |
| `src/ble/ble_server.{h,cpp}` | NimBLE GATT server, frame chunking, advertising data |
| `src/bridge/usb_bridge.{h,cpp}` | USB-CDC framed reader/writer (length-prefixed) |
| `src/config.h` | UUIDs, advertising name, MTU, OTA SSID, baud rate |
| `platformio.ini` | Build envs (`esp32-s3` for USB, `esp32-s3-ota` for OTA) |

The ESP32 itself is **stateless** — it doesn't parse any of the application-layer protocol. It just chunks/un-chunks BLE traffic and forwards bytes to/from USB. All packet decoding, passcode validation and Home-Assistant calls happen in the [`ha-bluetooth`](https://github.com/Gruni22/ha-bluetooth) custom component.

---

## BLE characteristics

| UUID | Property | Direction | Notes |
|------|----------|-----------|-------|
| `a10d4b1c-bf45-4c2a-9c32-4a8f7e3d1234` | Service | — | Advertised as the only primary service |
| `a10d4b1c-bf45-4c2a-9c32-4a8f7e3d1235` | Notify | ESP32 → App | TX. Up to 244 byte payload + 1 byte chunk flag per notification |
| `a10d4b1c-bf45-4c2a-9c32-4a8f7e3d1236` | Write / WriteNoResponse | App → ESP32 | RX. Same chunk format |

Standard GATT [Device Information Service](https://www.bluetooth.com/specifications/specs/device-information-service-1-1/) (`0x180A`) is also exposed (manufacturer, model, hardware revision, firmware version).

### Advertising packet

```
AD 0x01  Flags                : 0x06 (LE General Discoverable, BR/EDR not supported)
AD 0x07  Complete 128-bit UUID: a10d4b1c-bf45-4c2a-9c32-4a8f7e3d1234
AD 0xFF  Manufacturer Data    : 02 00 'H' 'A'
AD 0x08  Shortened Local Name : "HA Home"
```

Scan response packet adds **AD 0x09 Complete Local Name** = `Homeassistant_Home`.

### Security

- `setSecurityAuth(false, false, false)` — no bonding, no MITM protection, no Secure Connections.
- `deleteAllBonds()` is called on boot to scrub any leftover keys from older firmware that did use Secure Connections.
- All authentication is handled at the application layer by the 32-bit passcode in every packet (validated by the Pi).

---

## Configuration knobs

Edit `src/config.h` before flashing if needed:

| Define | Default | Effect |
|--------|---------|--------|
| `HA_DEVICE_NAME` | `"Homeassistant_Home"` | Full BLE name (scan response) |
| `HA_DEVICE_SHORT_NAME` | `"HA Home"` | Short name (primary ADV) |
| `BLE_MAX_CHUNK` | `244` | Max payload bytes per notification (= MTU 247 − 3 ATT) |
| `USB_SERIAL_BAUD` | `115200` | USB-CDC baud (cosmetic — CDC ignores baud) |
| `USB_MAX_FRAME` | `64 * 1024` | Max single frame size in either direction |
| `OTA_AP_SSID` | `"ESP32-HA-OTA"` | Wi-Fi SSID exposed during OTA |
| `OTA_HOSTNAME` | `"ha-gateway"` | mDNS name during OTA |

---

## Behaviour & guarantees

- **No `Serial.print*` on the data channel.** USB-CDC is the data link; any plain text is parsed as the next 4-byte frame length on the Pi side and corrupts framing. Logs are sent as JSON-encoded frames (`{"type":"log","msg":"…"}`) and consumed by the Pi.
- **32 KB USB-CDC RX buffer.** A `Serial.setRxBufferSize(32768)` call before `Serial.begin()` accommodates large frames (e.g. ANS_DEVICES ~30 KB) while BLE-Notify is busy draining a previous one (~2-3 s).
- **Notify retry loop.** `gTxChar->notify()` is retried up to 10 times with 50 ms backoff if the NimBLE MSYS pool is momentarily exhausted; only a real failure is reported back to the Pi.
- **20 ms inter-chunk pacing.** Multi-chunk frames pause briefly between chunks to let slow Android stacks deliver each notification before the next arrives.
- **Auto re-advertise.** On BLE disconnect the firmware immediately re-enters advertising mode.

---

## Troubleshooting

<details>
<summary><b>Pi cannot connect — <code>/dev/ttyACM0: No such file or directory</code></b></summary>

After reflash the kernel often assigns `/dev/ttyACM1` instead. Unplug the ESP32, wait 5 s, plug it back in. The HA integration v0.3+ uses stable `/dev/serial/by-id/…` paths from the auto-discovery dropdown, which avoids this entirely.
</details>

<details>
<summary><b>Phone connects but sync times out (cmd 0x10)</b></summary>

Usually stale NimBLE state from a previous client. **Power-cycle the ESP32 (unplug USB, wait 5 s, replug).** A simple reset button is *not* sufficient — only a power cycle clears all NimBLE buffers.
</details>

<details>
<summary><b>OTA upload fails with <code>No response from device</code></b></summary>

OTA mode requires BLE to be torn down first (Wi-Fi/BLE share the radio on ESP32-S3). Make sure the "Enable OTA" button was actually pressed in HA — the firmware only starts the AP after receiving an `ota_enable` frame on USB. Watch the HA log for `{"type":"ota_ready"}`.
</details>

<details>
<summary><b>Random reboots / watchdog resets under load</b></summary>

Common with USB charge-only cables. Switch to a known-good USB data cable, ideally short and shielded.
</details>

---

## Pin & power notes

- **Power:** USB bus power is sufficient. Sleep modes are *not* enabled (the gateway is meant to be always-on).
- **GPIO usage:** none. Only the on-chip USB peripheral is used; all GPIOs are free for future extensions.
- **TX power:** `NimBLEDevice::setPower(3)` = +3 dBm — a deliberate compromise between range and 2.4 GHz noise.

---

## Roadmap

- [ ] Optional BLE encryption (LE Secure Connections) when the HA passcode flow is bypassed
- [ ] Support for ESP32-C6 (cheaper, dual-band, longer-term replacement target)
- [ ] On-board OLED status display variant

---

## Contributing

Pull requests welcome. Please open an issue first for larger changes so the direction can be agreed on. Flash logs and `pio device monitor` output are extremely helpful for debugging — please attach them when reporting BLE issues.

## License

[MIT](LICENSE) — see `LICENSE` in the repo root.

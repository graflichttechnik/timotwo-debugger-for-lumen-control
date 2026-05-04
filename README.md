# TimoTwo SPI Register Debugger

Browser-based SPI register debugger for the [Radical Wireless](https://radicalwireless.com) **LUMEN CONTROL** module. Runs on an ESP32, serves a self-hosted web UI over WiFi — no dedicated hardware debugger needed.

---

## What it does

- Reads and writes all 29 TimoTwo SPI registers directly from a browser
- Live decoded output — hex bytes translated to human-readable values (protocols, frequencies, link state, RGB swatches, ASCII strings, etc.)
- R / W buttons per register, plus single-click **Read all**
- Full HTML/CSS/JS stored in ESP32 flash (PROGMEM) — no cloud, no dependencies
- Serial diagnostic logging at 115200 baud

---

## Hardware

**LUMEN CONTROL** is a compact wireless DMX/RDM module based on the LumenRadio **TimoTwo FX** — a switchable TX/RX transceiver supporting CRMX, W-DMX G3 and G4S, with BLE.

> RDM in RX mode is standard on all TimoTwo variants. RDM in TX mode requires a separately purchased dongle.

### Wiring — LUMEN CONTROL 10-pin header → ESP32

| Pin | Signal | GPIO | Note |
|:---:|:---:|:---:|:---|
| 1 | VDD | 3.3V | via 5VDC buck conversion|
| 2 | CS | 5 | |
| 3 | DMX+ | — | nc |
| 4 | SCK | 18 | |
| 5 | DMX− | — | nc |
| 6 | MOSI | 23 | |
| 7 | IRQ | 4 | input, pull-up |
| 8 | MISO | 19 | |
| 9 | LINK | 2 | optional |
| 10 | VSS | GND | |

> **ESP32-S3:** change `SPIClass timoSPI(VSPI)` → `(FSPI)`

---

## Requirements

- Arduino IDE 2.x
- ESP32 Arduino core ≥ 3.x (Espressif, via Boards Manager)
- No extra libraries — uses `SPI`, `WiFi`, `WebServer` from the core

---

## Installation & Usage

1. Open `timotwo_debugger.ino` in Arduino IDE
2. Select **ESP32 Dev Module**, choose the correct Port
3. Upload — hold **BOOT** if it stalls at `Connecting....`
4. Open Serial Monitor at **115200 baud** — confirm startup:
   ```
   [SPI] ready
   [WiFi] AP: TimoTwo-Debug  IP: 192.168.4.1
   [HTTP] Server running at http://192.168.4.1
   ```
5. Connect to WiFi **`TimoTwo-Debug`** (no password), open `http://192.168.4.1`

---

## REST API

| Method | Endpoint | Description |
|:---|:---|:---|
| GET | `/api/read?addr=0x00&len=1` | Read register |
| GET | `/api/write?addr=0x00&data=82` | Write register (hex bytes) |
| GET | `/api/readall` | Dump all readable registers |

```sh
# Enable radio, TX mode (CONFIG = 0x82)
curl "http://192.168.4.1/api/write?addr=0x00&data=82"
```

Full SPI protocol details → [PROTOCOL.md](PROTOCOL.md)

---

## Also Included

[`timotwo_driver.h`](timotwo_driver.h) — bare-metal SPI driver, no web server, no WiFi:

```cpp
#include "timotwo_driver.h"

void setup() {
    timoBegin();
    timoSetOEMInfo(0x0001, 0x08B6);   // model ID, ESTA manufacturer ID, i.e. 08B6
    timoSetDeviceName("Lumen Control");
    timoConfigTX(TIMO_RF_CRMX, 512);
}
void loop() {
    timoWriteDMX(dmx, 512);
}
```

---

## Files

| File | Description |
|:---|:---|
| `timotwo_debugger.ino` | Full web debugger (WiFi AP + browser UI) |
| `timotwo_driver.h` | Bare-metal SPI driver header |
| `timotwo_example.ino` | Minimal TX example using the driver |
| `PROTOCOL.md` | SPI protocol & command reference |

---

## Author

**Stefan Graf** · [stefan@graf-lichttechnik.de](mailto:stefan@graf-lichttechnik.de) · [graf-lichttechnik.de](https://graf-lichttechnik.de)

Developer of the Radical Wireless LUMEN CONTROL and LUMEN CARD modules.

---

## License

MIT — see [LICENSE](LICENSE)

---

## Related

- [LumenRadio TimoTwo Docs](https://docs.lumenrad.io/timotwo/) — official SPI register reference
- [Radical Wireless](https://radicalwireless.com) — LUMEN CONTROL / LUMEN CARD
- [tinkerberg.com](https://www.tinkerberg.com/wireless-dmx) — official distributor

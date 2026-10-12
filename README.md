# TimoTwo ESP32 Web SPI Debugger

Browser-based SPI register debugger for the Radical Wireless [**LUMEN CONTROL**](https://www.radicalwireless.com/products/lumen-control) module. It runs on an ESP32, joins your WiFi and serves a web UI. No dedicated hardware debugger needed.

Tested with LumenRadio TimoTwo FX firmware **1.1.1.5**.

---

## What it does

- Read and write all 29 TimoTwo registers from a browser, with the values decoded into readable text
- **Read all** in one click
- DMX test panel: send DMX (blackout, full, channel ranges, fade) and watch received DMX
- Asks for confirmation before a write that restarts the module
- Everything is stored on the ESP32, no cloud and no extra libraries

---

## Wiring: LUMEN CONTROL 10-pin header to ESP32-WROOM-32

| Pin | Signal | ESP32 GPIO | Note |
|:---:|:---:|:---:|:---|
| 1 | VDD | | 3.3 to 10 VDC |
| 2 | CS | 5 | |
| 3 | DMX+ | | XLR pin 3 with 120 Ω terminator, for the physical DMX output |
| 4 | SCK | 18 | |
| 5 | DMX- | | XLR pin 2 with 120 Ω terminator, for the physical DMX output |
| 6 | MOSI | 23 | |
| 7 | IRQ | 4 | input, pull-up, required |
| 8 | MISO | 19 | |
| 9 | LINK | 2 | optional |
| 10 | VSS | GND | |

---

## Requirements

- Arduino IDE 2.x with the ESP32 core from Espressif (Boards Manager), no extra libraries
- A 2.4 GHz WiFi network (the ESP32 cannot join 5 GHz)

---

## Installation and usage

1. Open the folder `timotwo-esp32-web-spi-debugger` in the Arduino IDE (open the `.ino` file inside it).
2. In that folder, copy `secrets.example.h` to `secrets.h` and enter your WiFi name and password.
3. Select **ESP32 Dev Module** and your port, then upload. Hold **BOOT** if it stalls at `Connecting....`
4. Open the Serial Monitor at 115200 baud. It shows the IP address and `http://timotwo-debug.local`.
5. Open `http://timotwo-debug.local` (or the IP address) in a browser on the same network.

> **Network:** The debugger has no password and can write any register. Use it only in a network you trust.

More: [HTTP API](API.md) · [SPI notes](PROTOCOL.md) · [driver and example](timotwo_example/README.md) · [release notes](CHANGELOG.md)

---

## Author

**Stefan Graf** · [stefan@graf-lichttechnik.de](mailto:stefan@graf-lichttechnik.de) · [graf-lichttechnik.de](https://graf-lichttechnik.de)

Developer of the Radical Wireless LUMEN CONTROL module.

---

## Disclaimer

Mistakes can happen. I have tried to test all functions, but I cannot rule out errors. Use the debugger at your own risk: I take no responsibility if something goes wrong. Writing registers changes the module, and some writes restart it or reset settings.

---

## License

MIT, see [LICENSE](LICENSE)

---

## Related

- [LumenRadio TimoTwo Docs](https://docs.lumenrad.io/timotwo/), official SPI register reference
- [Radical Wireless](https://radicalwireless.com), LUMEN CONTROL
- [tinkerberg.com](https://www.tinkerberg.com/wireless-dmx), official distributor

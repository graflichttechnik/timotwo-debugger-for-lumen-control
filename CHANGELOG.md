# Changelog

## 1.0.0: Fade In (October 2026)

Tested with LumenRadio TimoTwo FX firmware 1.1.1.5.

### Features

- The debugger joins your WiFi (2.4 GHz) instead of opening its own access point. Enter your WiFi in `secrets.h`, then open `http://timotwo-debug.local`.
- New DMX test panel: send DMX (blackout, full, channel ranges, fade) and watch received DMX.
- Registers that exist only in RX or only in TX mode are marked, and Read all shows the current mode.
- Writes that restart the module ask for confirmation first.
- The sketch now lives in the folder `timotwo-esp32-web-spi-debugger`. Open it in the Arduino IDE.

### Bug fixes

- OEM_INFO is now written in the correct byte order (manufacturer ID first, then model ID).
- Input is checked before anything is sent: register addresses outside the register map, characters that are not hex digits and data of the wrong length are rejected with an error message.


## 0.9.0: Preheat (May 2026)

First public version: the debugger opened its own access point (`TimoTwo-Debug`), read and wrote the TimoTwo registers and came with a bare-metal SPI driver header.

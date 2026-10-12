# TimoTwo SPI Notes

This debugger is based on the LumenRadio TimoTwo Development Documentation and the firmware Release Notes. Register definitions, bit meanings and the command list are not repeated here, please use the original:

- [SPI Interface](https://docs.lumenrad.io/timotwo/spi-interface/)
- [DMX Interface](https://docs.lumenrad.io/timotwo/dmx-interface/)
- [BLE Interface](https://docs.lumenrad.io/timotwo/ble-interface/)

Where the Dev Doc and the Release Notes differ, the Release Notes take precedence.

---

## How the debugger talks to the module

- SPI mode 0, 1 MHz, most significant bit first. The Dev Doc allows up to 8 MHz.
- A command is two SPI transactions. The first one sends the command byte, the first byte that comes back is `IRQ_FLAGS`. If bit 7 (`SPI_DEVICE_BUSY`) is set, the whole command is sent again. Then the debugger waits for the IRQ pin to go low, sends the payload (the first byte is ignored) and waits for the IRQ pin to go high.
- Registers are read with `00AAAAAA` and written with `01AAAAAA`, DMX is read with `0x81` and written with `0x91`.
- While the module reports busy, the debugger repeats the command for up to 30 ms with short pauses.
- A change between RX and TX mode reboots the module. The debugger then waits until it answers again (`PRODUCT_ID` reads `F1 40 00 00`) and reads `CONFIG` back to check the result.
- DMX goes over SPI in one block of up to 512 bytes. `READ_DMX` follows directly after writing `DMX_WINDOW`, with no other command in between.

---

## Own Findings

- **DMX block size:** The Dev Doc (DMX Interface page) says at most 128 slots per `WRITE_DMX` or `READ_DMX` command. The Release Notes say 512-byte blocks since firmware 1.1.1.0. 512-byte blocks work on a LUMEN CONTROL, so the Dev Doc page is outdated on this point.
- **DMX_WINDOW byte order:** The register table puts the window size in bytes 0 to 1 and the start address in bytes 2 to 3. The text below the table states the opposite. The debugger follows the table, which matches the reset value `02 00 00 00` (size 512, start 0).
- **BLE_STATUS bit polarity:** The value descriptions in the Dev Doc contradict the bit names. Measured twice on a LUMEN CONTROL with firmware 1.1.1.5: `01` without a BLE connection, `05` with a connected app, no PIN set. So bit 2 `BLE_IS_CONNECTED` = 1 means connected, as the bit name says. Bit 1 `PIN_ACTIVE` read 0 with no PIN set, which fits the bit name (1 = PIN active) and not the Dev Doc description. It still has to be confirmed with a PIN set. The debugger shows the raw bit values.
- **DEVICE_NAME and OEM_INFO:** A readback directly after a write was empty, with a pause of 2 s it was correct. The cause is not documented, the debugger waits 2 s after these writes.
- **RF_POWER:** The register table gives reset value 2 and maps it to 20 dBm from a 2 dBi antenna, the text names 18 dBm as the reset value.
- **Non-volatile registers:** The Dev Doc does not say which registers revert to defaults after a restart caused by `LINKING_KEY`, `BLE_ENABLE` or `BLE_PIN`.
- **Write length:** The Dev Doc says nothing about writing fewer bytes than the register length. The debugger always writes the full register length.

---

*Based on the LumenRadio TimoTwo Development Documentation and firmware Release Notes up to 1.1.1.5.
© LumenRadio AB · [docs.lumenrad.io/timotwo](https://docs.lumenrad.io/timotwo/)*

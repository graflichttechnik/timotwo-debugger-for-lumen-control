# TimoTwo SPI Protocol Reference

Full protocol details for the LumenRadio TimoTwo SPI interface.
See [docs.lumenrad.io/timotwo/spi-interface](https://docs.lumenrad.io/timotwo/spi-interface/) for the authoritative source.

---

## Interface Parameters

| Parameter | Value |
|:---|:---|
| Mode | SPI Mode 0 (CPOL=0, CPHA=0) |
| Bit order | MSB first |
| Byte order | Big-endian (network order) |
| Max clock | 2 MHz |
| CS setup time | 4 µs after CS low |
| Logic level | VDD max (5V for LUMEN CONTROL) |

---

## Transaction Structure

Every command (except NOP) consists of **two SPI transactions**:

```
Phase 1 — Command byte
  CS low → [4 µs] → transfer(cmd) → CS high
  Returned byte = IRQ_FLAGS
  If Bit7 (SPI_DEVICE_BUSY) = 1 → restart from Phase 1
  Otherwise → wait for IRQ to go LOW (command acknowledged)

Phase 2 — Payload
  CS low → [4 µs] → transfer(0xFF, dummy) → data → CS high
  Wait for IRQ to go HIGH (processing complete)
  Next command must not start until IRQ is HIGH
```

> **Critical:** Bit 7 of the first returned byte is `SPI_DEVICE_BUSY`. If set, the **entire** command sequence must be restarted — not just the payload phase.

---

## Command Bytes

| Command | Byte | Direction | Description |
|:---|:---:|:---:|:---|
| `READ_REG addr` | `0x00\|addr` | → | Read register (6-bit addr) |
| `WRITE_REG addr` | `0x40\|addr` | ← | Write register (6-bit addr) |
| `READ_DMX` | `0x81` | → | Read DMX window (set via DMX_WINDOW) |
| `READ_ASC` | `0x82` | → | Read last ASC frame |
| `READ_RDM` | `0x83` | → | Read RDM request (RX) or response (TX) |
| `READ_RXTX` | `0x84` | → | Read BLE RXTX data |
| `WRITE_DMX` | `0x91` | ← | Write DMX to internal TX buffer |
| `WRITE_RDM` | `0x92` | ← | Write RDM response (RX) or request (TX) |
| `WRITE_RXTX` | `0x93` | ← | Write BLE RXTX data |
| `RADIO_DISCOVERY` | `0xA0` | ← | Write Radio DUB command |
| `RADIO_DISCOVERY_RESULT` | `0xA1` | → | Read Radio DUB result |
| `RADIO_MUTE` | `0xA2` | ← | Write Radio mute command |
| `RADIO_MUTE_RESULT` | `0xA3` | → | Read Radio mute response |
| `RDM_DISCOVERY` | `0xA4` | ← | Write RDM DUB command |
| `RDM_DISCOVERY_RESULT` | `0xA5` | → | Read RDM DUB result |
| `NODE_QUERY` | `0xA6` | ← | Write radio node query |
| `NODE_QUERY_RESPONSE` | `0xA7` | → | Read radio node query response |
| `NOP` | `0xFF` | — | No-op; returns IRQ_FLAGS only |

---

## Register Map

All 29 registers. **Do not access undefined addresses** — undefined behaviour may result.

| Addr | Register | R/W | Len | Note |
|:---:|:---|:---:|:---:|:---|
| 0x00 | CONFIG | R/W | 1 | Write may cause reboot |
| 0x01 | STATUS | R/W | 1 | |
| 0x02 | IRQ_MASK | R/W | 1 | |
| 0x03 | IRQ_FLAGS | R | 1 | Bit7=SPI_BUSY |
| 0x04 | DMX_WINDOW | R/W | 4 | |
| 0x05 | ASC_FRAME | R | 3 | |
| 0x06 | LINK_QUALITY | R | 1 | RX only |
| 0x08 | DMX_SPEC | R/W | 8 | TX only |
| 0x09 | DMX_CONTROL | R/W | 1 | TX only |
| 0x0A | EXTENDED_IRQ_MASK | R/W | 4 | |
| 0x0B | EXTENDED_IRQ_FLAGS | R/W | 4 | Write to clear |
| 0x0C | RF_PROTOCOL | R/W | 1 | Write may cause reboot |
| 0x0D | DMX_SOURCE | R | 1 | |
| 0x0E | LOLLIPOP | R/W | 1 | Restart detector |
| 0x10 | VERSION | R | 8 | HW (4B) + FW (4B) |
| 0x11 | RF_POWER | R/W | 1 | TX only |
| 0x12 | BLOCKED_CHANNELS | R/W | 11 | TX only, min 16 active |
| 0x20 | BINDING_UID | R/W | 6 | RDM UID |
| 0x21 | LINKING_KEY | W | 10 | Write causes reboot |
| 0x30 | BLE_STATUS | R/W | 1 | Write causes reboot |
| 0x31 | BLE_PIN | W | 6 | Write causes reboot |
| 0x32 | BATTERY | W | 1 | BLE battery level |
| 0x33 | UNIVERSE_COLOR | R/W | 3 | RGB; RX=read-only |
| 0x34 | OEM_INFO | R/W | 4 | Model ID + ESTA ID |
| 0x35 | RXTX_STATUS | R | 1 | BLE RXTX service |
| 0x36 | DEVICE_NAME | R/W | 32 | ASCII, null-terminated |
| 0x37 | UNIVERSE_NAME | R | 16 | RX only, from TX |
| 0x3D | INSTALLED_OPTIONS | R | 13 | Paid options |
| 0x3F | PRODUCT_ID | R | 4 | `F1 40 00 00` |

---

## Key Register Details

### CONFIG (0x00)

| Bit | Name | Description |
|:---:|:---|:---|
| 7 | RADIO_ENABLE | 1 = radio on (default) |
| 3 | SPI_RDM | 1 = RDM via SPI, 0 = via UART (RX only) |
| 1 | TX/RX_MODE | 1 = TX, 0 = RX — **mode change triggers reboot** |
| 0 | UART_EN | 1 = UART DMX enabled (default) |

### IRQ_FLAGS (0x03) — returned on every Phase 1 transfer

| Bit | Name | Description |
|:---:|:---|:---|
| 7 | SPI_DEVICE_BUSY | Restart full command sequence if set |
| 6 | EXTENDED_IRQ | Check EXTENDED_IRQ_FLAGS |
| 5 | IDENTIFY_IRQ | RDM identify state changed |
| 4 | ASC_IRQ | ASC frame received |
| 3 | RF_LINK_IRQ | Radio link state changed |
| 2 | DMX_CHANGED_IRQ | DMX value in window changed |
| 1 | LOST_DMX_IRQ | DMX stream lost |
| 0 | RX_DMX_IRQ | Complete DMX frame received |

### LOLLIPOP (0x0E) — restart detection

Counts 0 → 255 → 128 → 129 → … on each read. Value **< 128** means module has rebooted since last check. Initialize to a value > 128 after setup, poll periodically.

### INSTALLED_OPTIONS (0x3D) — paid features

| Option ID | Feature |
|:---:|:---|
| `0x2001` | RDM TX via SPI |
| `0x2002` | RDM TX via Proxy |

---

## RF Power (TX only)

| Value | Power | EIRP with 2 dBi antenna |
|:---:|:---:|:---:|
| 2 | 18 dBm | 20 dBm / 100 mW *(default, ETSI compliant)* |
| 3 | 14 dBm | 16 dBm / 40 mW |
| 4 | 9 dBm | 11 dBm / 13 mW |
| 5 | 3 dBm | 5 dBm / 3 mW |

---

*Based on TimoTwo Development Documentation FW 1.0.7.2+
© 2019–2023 LumenRadio AB · [docs.lumenrad.io/timotwo](https://docs.lumenrad.io/timotwo/)*

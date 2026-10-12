# HTTP API

The web UI uses this API, and it can be used from scripts. Replace the host name by the IP address if mDNS does not work on your system.

| Method | Endpoint | Description |
|:---|:---|:---|
| GET | `/api/read?addr=0x34[&len=4]` | Read a register. `len`, when given, must equal the register length. |
| POST | `/api/write` | Write a register. Parameters `addr` and `data` in the form body or the query string. `data` must have exactly the register length. |
| GET | `/api/readall` | Read all readable registers |
| POST | `/api/dmx/write` | Form body `data=<hex>`, 1 to 512 channels, always writes 512 channels (padded with 0) |
| GET | `/api/dmx/read?start=0&count=64` | Read DMX channels, `start` 0 to 511, `count` 1 to 512, `start + count` at most 512 |

## Rules

- `addr` accepts decimal or `0x` hex. Only the 29 addresses of the register map are accepted.
- `data` is hex bytes, optional `0x` prefix, spaces allowed. LINKING_KEY needs 10 bytes in RX mode and 8 bytes in TX mode.
- Invalid input is answered with HTTP 400 and `{"ok":false,"error":"..."}`. Nothing is sent to the module in that case.
- If the module does not answer, the response is `{"ok":false,"error":"SPI timeout"}` with HTTP 200.
- `/api/readall` returns `{"ok":true,"mode":"rx","registers":{"0x34":{"ok":true,"bytes":[8,182,23,112]},...}}`. Registers of the other operating mode appear as `{"ok":false,"skipped":"tx_only"}` or `"rx_only"`.
- Writes that restart the module (CONFIG, RF_PROTOCOL, LINKING_KEY, BLE_STATUS, BLE_PIN) are followed by a wait until the module answers again. The response then contains `"restart":true` and `"ready":true|false`.
- Writes to DEVICE_NAME and OEM_INFO are answered after a 2 s pause.
- DMX is transferred in one block of up to 512 bytes per command.

## Examples

```sh
# Switch to TX mode (CONFIG = 0x83), the module restarts
curl -X POST -d "addr=0x00&data=83" http://timotwo-debug.local/api/write

# Write OEM_INFO: ESTA manufacturer ID 08B6, device model ID 1770
curl -X POST -d "addr=0x34&data=08B61770" http://timotwo-debug.local/api/write

# Read all registers
curl http://timotwo-debug.local/api/readall
```

Register details: [LumenRadio TimoTwo documentation](https://docs.lumenrad.io/timotwo/spi-interface/). Own findings: [PROTOCOL.md](PROTOCOL.md)

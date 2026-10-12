# Driver and example

`timotwo_driver.h` is a bare-metal SPI driver for the TimoTwo, without web server and WiFi. Copy it into your own sketch folder. All functions are `inline`, so the header can be included from several source files.

`timotwo_example.ino` is a minimal TX example: TX mode, labels, DMX fade on channels 1 to 16.

```cpp
#include "timotwo_driver.h"

static uint8_t dmx[512];

void setup() {
    timoBegin();
    timoConfigTX(TIMO_RF_CRMX, 512);               // a mode change restarts the module
    timoSetOEMInfo(0x08B6, 0x0001);                // ESTA manufacturer ID, device model ID
    timoSetDeviceName("Lumen Control");            // labels last, a mode change resets registers
}
void loop() {
    timoWriteDMX(dmx, 512);
}
```

Wiring as in the [main README](../README.md). Register details: [LumenRadio TimoTwo documentation](https://docs.lumenrad.io/timotwo/spi-interface/). Own findings: [PROTOCOL.md](../PROTOCOL.md)

/**
 * timotwo_example.ino
 * ─────────────────────────────────────────────────────────────
 *  Bare-metal TimoTwo usage example
 *  Include timotwo_driver.h in the same sketch folder.
 *  Version: 1.0.0 - October 2026
 * ─────────────────────────────────────────────────────────────
 */

#include "timotwo_driver.h"

// ── DMX universe ──────────────────────────────────────────────
static uint8_t dmx[512] = {};

void setup() {
    Serial.begin(115200);
    delay(400);

    timoBegin();
    delay(300);  // allow module to boot

    // Verify module is present
    if (!timoDetect()) {
        Serial.println("ERROR: TimoTwo not found, check wiring");
        while (true) delay(1000);
    }
    timoPrintVersion();

    // Init TX mode: CRMX, 512 channels, 40 Hz.
    // A mode change restarts the module and resets its registers,
    // so names and metadata are written afterwards.
    if (!timoConfigTX(TIMO_RF_CRMX, 512, 25000)) {
        Serial.println("ERROR: TX config failed");
        while (true) delay(1000);
    }

    // Set OEM info: ESTA manufacturer ID 0x08B6, model ID 0x0001
    timoSetOEMInfo(0x08B6, 0x0001);

    // Set device name
    timoSetDeviceName("Lumen Control");

    // Set universe color (optional, visible in the CRMX Toolbox app)
    timoSetUniverseColor(0xFF, 0x80, 0x00);  // orange

    Serial.println("Ready: press the LINK button on the module to pair");
}

void loop() {
    // ── Example: simple DMX fade on channels 1 to 16 ──────────
    static uint8_t  level = 0;
    static int8_t   dir   = 1;
    static uint32_t lastTx = 0;

    if (millis() - lastTx >= 25) {  // ~40 Hz
        lastTx = millis();

        for (int i = 0; i < 16; i++) dmx[i] = level;
        timoWriteDMX(dmx, 512);

        level += dir * 3;
        if (level >= 252) dir = -1;
        if (level <= 3)   dir =  1;
    }

    // ── Periodic status output ────────────────────────────────
    static uint32_t lastStatus = 0;
    if (millis() - lastStatus >= 2000) {
        lastStatus = millis();
        Serial.printf("Linked: %s  RF: %s  Quality: %d  Level: %d\n",
            timoIsLinked()   ? "yes" : "no",
            timoHasRFLink()  ? "yes" : "no",
            timoLinkQuality(),
            level);
    }
}

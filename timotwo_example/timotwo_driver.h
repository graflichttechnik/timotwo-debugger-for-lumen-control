/**
 * timotwo_driver.h
 * ─────────────────────────────────────────────────────────────
 *  LumenRadio TimoTwo: bare-metal SPI driver
 *  Target: ESP32-WROOM-32 (VSPI) | Arduino framework
 *  Version: 1.0.0 - October 2026
 *  Tested with: TimoTwo FX firmware 1.1.1.5
 *
 *  No WiFi. No web server. No HTML. Just SPI.
 *  All functions are inline: the header can be included from more
 *  than one source file.
 *
 *  Wiring (LUMEN CONTROL 10-pin header):
 *    Pin  1  VDD  → 3.3 to 10 VDC
 *    Pin  2  CS   → GPIO  5
 *    Pin  4  SCK  → GPIO 18
 *    Pin  6  MOSI → GPIO 23
 *    Pin  7  IRQ  → GPIO  4  (input, pull-up)
 *    Pin  8  MISO → GPIO 19
 *    Pin  9  LINK → GPIO  2  (optional)
 *    Pin 10  VSS  → GND
 * ─────────────────────────────────────────────────────────────
 *  SPI protocol summary (LumenRadio TimoTwo Development Documentation):
 *
 *  Every command is two-phase:
 *
 *    Phase 1: command byte
 *      CS low → [4µs setup] → transfer(cmd) → CS high
 *      The byte returned is IRQ_FLAGS. If bit 7 (SPI_DEVICE_BUSY) is set,
 *      the whole sequence is restarted. Otherwise wait for IRQ to go LOW
 *      (command acknowledged).
 *
 *    Phase 2: payload
 *      CS low → [4µs setup] → transfer(0xFF, ignored) → data → CS high
 *      Wait for IRQ to go HIGH (processing complete).
 *
 *  Command byte encoding:
 *    READ_REG  addr:  0b00AA_AAAA  (0x00 | addr)
 *    WRITE_REG addr:  0b01AA_AAAA  (0x40 | addr)
 *    READ_DMX:        0x81
 *    WRITE_DMX:       0x91
 *    NOP:             0xFF
 * ─────────────────────────────────────────────────────────────
 */

#pragma once
#include <SPI.h>

// ── Pin configuration ─────────────────────────────────────────
#define TIMO_PIN_CS     5
#define TIMO_PIN_SCK   18
#define TIMO_PIN_MOSI  23
#define TIMO_PIN_MISO  19
#define TIMO_PIN_IRQ    4
#define TIMO_PIN_LINK   2

// ── SPI settings ──────────────────────────────────────────────
#define TIMO_SPI_FREQ        1000000UL   // 1 MHz (Dev Doc maximum: 8 MHz)
#define TIMO_IRQ_TIMEOUT_MS  200
#define TIMO_BUSY_BUDGET_MS  30          // a command sequence is restarted for this long while the module reports busy
#define TIMO_RESTART_WAIT_MS 300         // wait after a write that restarts the module
#define TIMO_RESTART_POLL_MS 3000        // then poll PRODUCT_ID for up to this long
#define TIMO_LABEL_SETTLE_MS 2000        // wait after DEVICE_NAME / OEM_INFO writes

// ── SPI commands ──────────────────────────────────────────────
#define TIMO_CMD_READ(addr)   ((uint8_t)((addr) & 0x3F))
#define TIMO_CMD_WRITE(addr)  ((uint8_t)(0x40 | ((addr) & 0x3F)))
#define TIMO_CMD_READ_DMX     0x81
#define TIMO_CMD_WRITE_DMX    0x91
#define TIMO_CMD_NOP          0xFF

// ── Register addresses ────────────────────────────────────────
#define TIMO_REG_CONFIG          0x00
#define TIMO_REG_STATUS          0x01
#define TIMO_REG_IRQ_MASK        0x02
#define TIMO_REG_IRQ_FLAGS       0x03
#define TIMO_REG_DMX_WINDOW      0x04
#define TIMO_REG_ASC_FRAME       0x05
#define TIMO_REG_LINK_QUALITY    0x06
#define TIMO_REG_DMX_SPEC        0x08
#define TIMO_REG_DMX_CONTROL     0x09
#define TIMO_REG_EXT_IRQ_MASK    0x0A
#define TIMO_REG_EXT_IRQ_FLAGS   0x0B
#define TIMO_REG_RF_PROTOCOL     0x0C
#define TIMO_REG_DMX_SOURCE      0x0D
#define TIMO_REG_LOLLIPOP        0x0E
#define TIMO_REG_VERSION         0x10
#define TIMO_REG_RF_POWER        0x11
#define TIMO_REG_BLOCKED_CH      0x12
#define TIMO_REG_BINDING_UID     0x20
#define TIMO_REG_LINKING_KEY     0x21
#define TIMO_REG_BLE_STATUS      0x30
#define TIMO_REG_BLE_PIN         0x31
#define TIMO_REG_BATTERY         0x32
#define TIMO_REG_UNIVERSE_COLOR  0x33
#define TIMO_REG_OEM_INFO        0x34
#define TIMO_REG_RXTX_STATUS     0x35
#define TIMO_REG_DEVICE_NAME     0x36
#define TIMO_REG_UNIVERSE_NAME   0x37
#define TIMO_REG_INSTALLED_OPT   0x3D
#define TIMO_REG_PRODUCT_ID      0x3F

// ── CONFIG register bits ──────────────────────────────────────
#define TIMO_CONFIG_RADIO_EN   (1 << 7)
#define TIMO_CONFIG_SPI_RDM    (1 << 3)
#define TIMO_CONFIG_TX_MODE    (1 << 1)   // 1=TX, 0=RX
#define TIMO_CONFIG_UART_EN    (1 << 0)

// ── STATUS register bits ──────────────────────────────────────
#define TIMO_STATUS_UPDATE_MODE (1 << 7)
#define TIMO_STATUS_DMX         (1 << 3)
#define TIMO_STATUS_IDENTIFY    (1 << 2)
#define TIMO_STATUS_RF_LINK     (1 << 1)
#define TIMO_STATUS_LINKED      (1 << 0)

// ── IRQ_FLAGS Bit7 = SPI_DEVICE_BUSY ─────────────────────────
#define TIMO_IRQ_SPI_BUSY  (1 << 7)

// ── RF protocol values ────────────────────────────────────────
#define TIMO_RF_CRMX       0x00
#define TIMO_RF_WDMX_G3    0x01
#define TIMO_RF_WDMX_G4S   0x03

// ── RF power values (TX only) ─────────────────────────────────
// Dev Doc: output power from a 2 dBi antenna (EIRP). Reset value is 2.
#define TIMO_PWR_20DBM     0x02   // 100 mW
#define TIMO_PWR_16DBM     0x03
#define TIMO_PWR_11DBM     0x04
#define TIMO_PWR_5DBM      0x05

// ── Known product ID ──────────────────────────────────────────
#define TIMO_PRODUCT_ID_0  0xF1
#define TIMO_PRODUCT_ID_1  0x40

// ── Internal SPI bus instance (one instance across all source files) ──
inline SPIClass& _timoSPI() {
    static SPIClass bus(VSPI);
    return bus;
}

// ─────────────────────────────────────────────────────────────
//  Low-level helpers
// ─────────────────────────────────────────────────────────────

// Wait for IRQ pin to reach targetState within timeout
inline bool _timoWaitIRQ(bool targetState, uint32_t timeoutMs = TIMO_IRQ_TIMEOUT_MS) {
    uint32_t t = millis();
    while ((bool)digitalRead(TIMO_PIN_IRQ) != targetState) {
        if (millis() - t > timeoutMs) return false;
        delayMicroseconds(10);
    }
    return true;
}

// One complete command sequence (command byte, IRQ low, payload, IRQ high).
// A set SPI_DEVICE_BUSY flag restarts the sequence, with a short pause, for up to TIMO_BUSY_BUDGET_MS.
// Write: wbuf is sent. Read: rbuf receives len bytes.
inline bool _timoCommand(uint8_t cmd, const uint8_t* wbuf, uint8_t* rbuf, uint16_t len) {
    SPIClass& spi = _timoSPI();
    const uint32_t t0 = millis();
    for (;;) {
        spi.beginTransaction(SPISettings(TIMO_SPI_FREQ, MSBFIRST, SPI_MODE0));
        digitalWrite(TIMO_PIN_CS, LOW);
        delayMicroseconds(4);
        uint8_t flags = spi.transfer(cmd);
        digitalWrite(TIMO_PIN_CS, HIGH);
        spi.endTransaction();
        if (!(flags & TIMO_IRQ_SPI_BUSY)) break;
        if (millis() - t0 > TIMO_BUSY_BUDGET_MS) return false;     // stayed busy
        _timoWaitIRQ(true, 5);
        delayMicroseconds(500);
    }
    if (!_timoWaitIRQ(false)) return false;          // command not acknowledged

    spi.beginTransaction(SPISettings(TIMO_SPI_FREQ, MSBFIRST, SPI_MODE0));
    digitalWrite(TIMO_PIN_CS, LOW);
    delayMicroseconds(4);
    spi.transfer(0xFF);                              // first byte is ignored by the module
    if (wbuf) for (uint16_t i = 0; i < len; i++) spi.transfer(wbuf[i]);
    else      for (uint16_t i = 0; i < len; i++) rbuf[i] = spi.transfer(0x00);
    digitalWrite(TIMO_PIN_CS, HIGH);
    spi.endTransaction();

    return _timoWaitIRQ(true);
}

// ─────────────────────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────────────────────

// Initialize GPIOs and SPI bus. Call once in setup().
inline void timoBegin() {
    pinMode(TIMO_PIN_CS,   OUTPUT); digitalWrite(TIMO_PIN_CS, HIGH);
    pinMode(TIMO_PIN_IRQ,  INPUT_PULLUP);
    pinMode(TIMO_PIN_LINK, INPUT_PULLUP);
    _timoSPI().begin(TIMO_PIN_SCK, TIMO_PIN_MISO, TIMO_PIN_MOSI, TIMO_PIN_CS);
}

// Read `len` bytes from `addr` into `buf`. Returns true on success.
inline bool timoRead(uint8_t addr, uint8_t* buf, uint8_t len) {
    if (!buf || len == 0 || len > 64) return false;
    return _timoCommand(TIMO_CMD_READ(addr), nullptr, buf, len);
}

// Convenience: read single byte
inline uint8_t timoRead8(uint8_t addr) {
    uint8_t v = 0;
    timoRead(addr, &v, 1);
    return v;
}

// Convenience: read 16-bit big-endian
inline uint16_t timoRead16(uint8_t addr) {
    uint8_t b[2] = {};
    timoRead(addr, b, 2);
    return ((uint16_t)b[0] << 8) | b[1];
}

// Write `len` bytes from `buf` to `addr`. Returns true on success.
inline bool timoWrite(uint8_t addr, const uint8_t* buf, uint8_t len) {
    if (!buf || len == 0 || len > 64) return false;
    return _timoCommand(TIMO_CMD_WRITE(addr), buf, nullptr, len);
}

// Convenience: write single byte
inline bool timoWrite8(uint8_t addr, uint8_t value) {
    return timoWrite(addr, &value, 1);
}

// Write DMX payload (1 to 512 channels) to the internal TX buffer.
// One WRITE_DMX block (blocks of up to 512 bytes: firmware 1.1.1.0 and later).
inline bool timoWriteDMX(const uint8_t* channels, uint16_t count) {
    if (!channels || count == 0 || count > 512) return false;
    return _timoCommand(TIMO_CMD_WRITE_DMX, channels, nullptr, count);
}

// Read `count` channels starting at zero-indexed channel `start`.
// Sets DMX_WINDOW, then reads the window with one READ_DMX block.
// No other SPI command may sit between the two.
inline bool timoReadDMX(uint8_t* channels, uint16_t count, uint16_t start = 0) {
    if (!channels || count == 0 || start >= 512 || start + count > 512) return false;
    // DMX_WINDOW: bytes 0-1 window size, bytes 2-3 start address (big-endian)
    uint8_t win[4] = {
        (uint8_t)(count >> 8), (uint8_t)(count & 0xFF),
        (uint8_t)(start >> 8), (uint8_t)(start & 0xFF)
    };
    if (!timoWrite(TIMO_REG_DMX_WINDOW, win, 4)) return false;
    return _timoCommand(TIMO_CMD_READ_DMX, nullptr, channels, count);
}

// ─────────────────────────────────────────────────────────────
//  Higher-level helpers
// ─────────────────────────────────────────────────────────────

// Returns true if the PRODUCT_ID register matches a known TimoTwo
inline bool timoDetect() {
    uint8_t id[4] = {};
    if (!timoRead(TIMO_REG_PRODUCT_ID, id, 4)) return false;
    return (id[0] == TIMO_PRODUCT_ID_0 && id[1] == TIMO_PRODUCT_ID_1);
}

// After a write that restarts the module: wait, then poll PRODUCT_ID until it answers.
// Returns false if the module does not answer within TIMO_RESTART_POLL_MS.
inline bool timoWaitReady() {
    delay(TIMO_RESTART_WAIT_MS);
    uint32_t t0 = millis();
    while (millis() - t0 < TIMO_RESTART_POLL_MS) {
        if (timoDetect()) return true;
        delay(50);
    }
    return false;
}

// Print hardware and firmware version to Serial
inline void timoPrintVersion() {
    uint8_t v[8] = {};
    if (!timoRead(TIMO_REG_VERSION, v, 8)) {
        Serial.println("[TimoTwo] version read failed");
        return;
    }
    Serial.printf("[TimoTwo] HW: %02X%02X%02X%02X  FW: %d.%d.%d.%d\n",
        v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
}

// Write a CONFIG value, wait for the module to answer again, and decide the result by
// reading CONFIG back (the write acknowledge can be lost when the module restarts).
// Only bit 1 (TX/RX mode) is compared: a mode change resets the other bits to reset values.
inline bool _timoWriteConfig(uint8_t cfg) {
    uint8_t before = timoRead8(TIMO_REG_CONFIG);
    bool acked = timoWrite8(TIMO_REG_CONFIG, cfg);
    bool modeChanged = ((before ^ cfg) & TIMO_CONFIG_TX_MODE) != 0;
    if (!acked || modeChanged) {
        if (!timoWaitReady()) return false;
    }
    uint8_t after = timoRead8(TIMO_REG_CONFIG);
    return ((after ^ cfg) & TIMO_CONFIG_TX_MODE) == 0 && (acked || modeChanged);
}

// Configure TX mode (call before timoEnableRadio)
// protocol: TIMO_RF_CRMX / TIMO_RF_WDMX_G3 / TIMO_RF_WDMX_G4S
// channels: 1 to 512
// refreshUs: refresh period in µs (25000 = 40 Hz)
// A mode change restarts the module and resets all registers, so the rest is written afterwards.
inline bool timoConfigTX(uint8_t protocol, uint16_t channels, uint32_t refreshUs = 25000) {
    // CONFIG: enable radio, TX mode, UART off
    if (!_timoWriteConfig(TIMO_CONFIG_RADIO_EN | TIMO_CONFIG_TX_MODE)) return false;

    // RF_PROTOCOL write can restart the module and swallow the acknowledge,
    // so the result is decided by the readback
    timoWrite8(TIMO_REG_RF_PROTOCOL, protocol);
    if (!timoWaitReady()) return false;
    if (timoRead8(TIMO_REG_RF_PROTOCOL) != protocol) return false;

    if (!timoWrite8(TIMO_REG_RF_POWER, TIMO_PWR_20DBM)) return false;

    // DMX_SPEC: channel count + refresh period
    uint8_t spec[8] = {
        (uint8_t)(channels >> 8), (uint8_t)(channels & 0xFF),  // N_CHANNELS
        0x00, 0x00,                                              // interslot time
        (uint8_t)(refreshUs >> 24), (uint8_t)(refreshUs >> 16),
        (uint8_t)(refreshUs >> 8),  (uint8_t)(refreshUs)        // refresh period
    };
    if (!timoWrite(TIMO_REG_DMX_SPEC, spec, 8)) return false;

    // all 512 slots are written once (zeros) before the output is enabled
    static const uint8_t zeros[512] = {};
    if (!timoWriteDMX(zeros, 512)) return false;

    if (!timoWrite8(TIMO_REG_DMX_CONTROL, 0x01)) return false;  // enable DMX gen

    return true;
}

// Configure RX mode
inline bool timoConfigRX() {
    return _timoWriteConfig(TIMO_CONFIG_RADIO_EN);   // TX_MODE bit = 0 → RX
}

// Enable or disable the radio (CONFIG bit 7)
inline bool timoEnableRadio(bool enable = true) {
    uint8_t cfg = timoRead8(TIMO_REG_CONFIG);
    if (enable) cfg |=  TIMO_CONFIG_RADIO_EN;
    else        cfg &= ~TIMO_CONFIG_RADIO_EN;
    return _timoWriteConfig(cfg);
}

// Set device name (Toolbox app, RDM DEVICE_LABEL, TX universe name)
// name: null-terminated ASCII, up to 31 characters (32-byte field, null-padded).
// In TX mode the first 16 characters are sent as universe name.
// Write the name after the last mode change, because a mode change resets registers.
// Pauses TIMO_LABEL_SETTLE_MS afterwards (observed: a readback directly after the
// write was empty; the cause is not documented).
inline bool timoSetDeviceName(const char* name) {
    uint8_t buf[32] = {};
    strncpy((char*)buf, name, 31);
    bool ok = timoWrite(TIMO_REG_DEVICE_NAME, buf, 32);
    if (ok) delay(TIMO_LABEL_SETTLE_MS);
    return ok;
}

// Set OEM info: ESTA manufacturer ID (bytes 0-1), device model ID (bytes 2-3), big-endian.
// Example: timoSetOEMInfo(0x08B6, 0x0001)  (manufacturer 08B6, model 1)
// Pauses TIMO_LABEL_SETTLE_MS afterwards, like timoSetDeviceName().
inline bool timoSetOEMInfo(uint16_t estaId, uint16_t modelId) {
    uint8_t buf[4] = {
        (uint8_t)(estaId  >> 8), (uint8_t)(estaId  & 0xFF),
        (uint8_t)(modelId >> 8), (uint8_t)(modelId & 0xFF)
    };
    bool ok = timoWrite(TIMO_REG_OEM_INFO, buf, 4);
    if (ok) delay(TIMO_LABEL_SETTLE_MS);
    return ok;
}

// Set universe color (RGB, 3 bytes, TX mode)
inline bool timoSetUniverseColor(uint8_t r, uint8_t g, uint8_t b) {
    uint8_t buf[3] = {r, g, b};
    return timoWrite(TIMO_REG_UNIVERSE_COLOR, buf, 3);
}

// Returns true if the STATUS LINKED bit is set (linked, or pairing in progress)
inline bool timoHasLinkedFlag() {
    return (timoRead8(TIMO_REG_STATUS) & TIMO_STATUS_LINKED) != 0;
}

// Returns true if the RF_LINK bit is set (active radio link)
inline bool timoHasRFLink() {
    return (timoRead8(TIMO_REG_STATUS) & TIMO_STATUS_RF_LINK) != 0;
}

// Returns true for a valid link: LINKED and RF_LINK are both set
inline bool timoIsLinked() {
    uint8_t s = timoRead8(TIMO_REG_STATUS);
    return (s & TIMO_STATUS_LINKED) && (s & TIMO_STATUS_RF_LINK);
}

// Unlink: write 1 to STATUS bit 0 (not via the LINK pin), then wait until
// LINKED and RF_LINK are both clear. Returns false on timeout.
inline bool timoUnlink(uint32_t timeoutMs = 3000) {
    if (!timoWrite8(TIMO_REG_STATUS, TIMO_STATUS_LINKED)) return false;
    uint32_t t0 = millis();
    while (millis() - t0 < timeoutMs) {
        uint8_t s = timoRead8(TIMO_REG_STATUS);
        if (!(s & (TIMO_STATUS_LINKED | TIMO_STATUS_RF_LINK))) return true;
        delay(50);
    }
    return false;
}

// Returns link quality 0 to 255 (RX only)
inline uint8_t timoLinkQuality() {
    return timoRead8(TIMO_REG_LINK_QUALITY);
}

// Returns IRQ_FLAGS byte (check Bit7 for SPI_BUSY, others for events)
inline uint8_t timoIRQFlags() {
    return timoRead8(TIMO_REG_IRQ_FLAGS);
}

// Check if a specific installed option is present
// knownOptions: 0x2001 = RDM TX via SPI, 0x2002 = RDM TX via proxy
inline bool timoHasOption(uint16_t optionCode) {
    uint8_t buf[13] = {};
    if (!timoRead(TIMO_REG_INSTALLED_OPT, buf, 13)) return false;
    uint8_t count = buf[0];
    for (uint8_t i = 0; i < count && i < 6; i++) {
        uint16_t opt = ((uint16_t)buf[1 + i*2] << 8) | buf[2 + i*2];
        if (opt == optionCode) return true;
    }
    return false;
}

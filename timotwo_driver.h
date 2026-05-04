/**
 * timotwo_driver.h
 * ─────────────────────────────────────────────────────────────
 *  Made for Radical Wireless Lumenradio TimoTwo — bare-metal SPI driver
 *  by Stefan Graf stefan-graf-lichttechnik.de - April 2026
 *  github page: https://github.com/graflichttechnik
 *
 *  Target: ESP32 dev boards, like ESP32 Wroom 32 or ESP32 S3 (via VSPI) | Arduino framework
 *
 *  Wiring (LUMEN CONTROL 10-pin header):
 *    Pin  1  VDD  → 5V (buck conversion to 3.3V)
 *    Pin  2  CS   → GPIO  5
 *    Pin  3  DMX+ → GPIO not assigned (optional)
 *    Pin  4  SCK  → GPIO 18
 *    Pin  5  DMX- → GPIO not assigned (optional)
 *    Pin  6  MOSI → GPIO 23
 *    Pin  7  IRQ  → GPIO  4  (input, pull-up)
 *    Pin  8  MISO → GPIO 19
 *    Pin  9  LINK → GPIO  2  (optional)
 *    Pin 10  VSS  → GND
 * ─────────────────────────────────────────────────────────────
 *  SPI Specifications:
 *
 *  Every transaction is two-phase:
 *
 *    Phase 1 — Command byte:
 *      CS low → [4µs setup] → transfer(cmd) → CS high
 *      If returned byte has Bit7 set: SPI_DEVICE_BUSY, retry.
 *      Otherwise: wait for IRQ to go LOW (command acknowledged).
 *
 *    Phase 2 — Payload:
 *      CS low → [4µs setup] → transfer(0xFF, ignored) → data → CS high
 *      Wait for IRQ to go HIGH (transfer complete).
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
#define TIMO_SPI_FREQ   1000000UL   // 1 MHz — max allowed is 2 MHz
#define TIMO_IRQ_TIMEOUT_MS  200

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
#define TIMO_PWR_20DBM     0x02   // 100 mW
#define TIMO_PWR_16DBM     0x03
#define TIMO_PWR_11DBM     0x04
#define TIMO_PWR_5DBM      0x05

// ── Known product ID ──────────────────────────────────────────
#define TIMO_PRODUCT_ID_0  0xF1
#define TIMO_PRODUCT_ID_1  0x40

// ── Internal SPI bus instance ─────────────────────────────────
static SPIClass _timoSPI(VSPI);

// ─────────────────────────────────────────────────────────────
//  Low-level helpers
// ─────────────────────────────────────────────────────────────

// Wait for IRQ pin to reach targetState within timeout
static bool _timoWaitIRQ(bool targetState, uint32_t timeoutMs = TIMO_IRQ_TIMEOUT_MS) {
    uint32_t t = millis();
    while ((bool)digitalRead(TIMO_PIN_IRQ) != targetState) {
        if (millis() - t > timeoutMs) return false;
        delayMicroseconds(10);
    }
    return true;
}

// Send a single command byte; returns the IRQ_FLAGS byte received
static uint8_t _timoSendCmd(uint8_t cmd) {
    _timoSPI.beginTransaction(SPISettings(TIMO_SPI_FREQ, MSBFIRST, SPI_MODE0));
    digitalWrite(TIMO_PIN_CS, LOW);
    delayMicroseconds(4);
    uint8_t flags = _timoSPI.transfer(cmd);
    digitalWrite(TIMO_PIN_CS, HIGH);
    _timoSPI.endTransaction();
    return flags;
}

// ─────────────────────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────────────────────

// Initialize GPIOs and SPI bus. Call once in setup().
void timoBegin() {
    pinMode(TIMO_PIN_CS,   OUTPUT); digitalWrite(TIMO_PIN_CS, HIGH);
    pinMode(TIMO_PIN_IRQ,  INPUT_PULLUP);
    pinMode(TIMO_PIN_LINK, INPUT_PULLUP);
    _timoSPI.begin(TIMO_PIN_SCK, TIMO_PIN_MISO, TIMO_PIN_MOSI, TIMO_PIN_CS);
}

// Read `len` bytes from `addr` into `buf`. Returns true on success.
bool timoRead(uint8_t addr, uint8_t* buf, uint8_t len) {
    if (!buf || len == 0 || len > 64) return false;

    uint8_t flags = _timoSendCmd(TIMO_CMD_READ(addr));

    if (flags & TIMO_IRQ_SPI_BUSY) {
        _timoWaitIRQ(true);
        return timoRead(addr, buf, len);   // retry
    }
    if (!_timoWaitIRQ(false)) return false;

    _timoSPI.beginTransaction(SPISettings(TIMO_SPI_FREQ, MSBFIRST, SPI_MODE0));
    digitalWrite(TIMO_PIN_CS, LOW);
    delayMicroseconds(4);
    _timoSPI.transfer(0xFF);               // dummy byte, discarded by TimoTwo
    for (uint8_t i = 0; i < len; i++) buf[i] = _timoSPI.transfer(0x00);
    digitalWrite(TIMO_PIN_CS, HIGH);
    _timoSPI.endTransaction();

    return _timoWaitIRQ(true);
}

// Convenience: read single byte
uint8_t timoRead8(uint8_t addr) {
    uint8_t v = 0;
    timoRead(addr, &v, 1);
    return v;
}

// Convenience: read 16-bit big-endian
uint16_t timoRead16(uint8_t addr) {
    uint8_t b[2] = {};
    timoRead(addr, b, 2);
    return ((uint16_t)b[0] << 8) | b[1];
}

// Write `len` bytes from `buf` to `addr`. Returns true on success.
bool timoWrite(uint8_t addr, const uint8_t* buf, uint8_t len) {
    if (!buf || len == 0 || len > 64) return false;

    uint8_t flags = _timoSendCmd(TIMO_CMD_WRITE(addr));

    if (flags & TIMO_IRQ_SPI_BUSY) {
        _timoWaitIRQ(true);
        return timoWrite(addr, buf, len);  // retry
    }
    if (!_timoWaitIRQ(false)) return false;

    _timoSPI.beginTransaction(SPISettings(TIMO_SPI_FREQ, MSBFIRST, SPI_MODE0));
    digitalWrite(TIMO_PIN_CS, LOW);
    delayMicroseconds(4);
    _timoSPI.transfer(0xFF);               // dummy byte
    for (uint8_t i = 0; i < len; i++) _timoSPI.transfer(buf[i]);
    digitalWrite(TIMO_PIN_CS, HIGH);
    _timoSPI.endTransaction();

    return _timoWaitIRQ(true);
}

// Convenience: write single byte
bool timoWrite8(uint8_t addr, uint8_t value) {
    return timoWrite(addr, &value, 1);
}

// Write DMX payload (up to 512 channels) to TX buffer
bool timoWriteDMX(const uint8_t* channels, uint16_t count) {
    if (!channels || count == 0) return false;
    if (count > 512) count = 512;

    uint8_t flags = _timoSendCmd(TIMO_CMD_WRITE_DMX);

    if (flags & TIMO_IRQ_SPI_BUSY) {
        _timoWaitIRQ(true);
        return timoWriteDMX(channels, count);
    }
    if (!_timoWaitIRQ(false)) return false;

    _timoSPI.beginTransaction(SPISettings(TIMO_SPI_FREQ, MSBFIRST, SPI_MODE0));
    digitalWrite(TIMO_PIN_CS, LOW);
    delayMicroseconds(4);
    _timoSPI.transfer(0xFF);
    for (uint16_t i = 0; i < count; i++) _timoSPI.transfer(channels[i]);
    digitalWrite(TIMO_PIN_CS, HIGH);
    _timoSPI.endTransaction();

    return _timoWaitIRQ(true);
}

// Read DMX payload from RX buffer (up to 512 channels)
bool timoReadDMX(uint8_t* channels, uint16_t count) {
    if (!channels || count == 0) return false;
    if (count > 512) count = 512;

    uint8_t flags = _timoSendCmd(TIMO_CMD_READ_DMX);

    if (flags & TIMO_IRQ_SPI_BUSY) {
        _timoWaitIRQ(true);
        return timoReadDMX(channels, count);
    }
    if (!_timoWaitIRQ(false)) return false;

    _timoSPI.beginTransaction(SPISettings(TIMO_SPI_FREQ, MSBFIRST, SPI_MODE0));
    digitalWrite(TIMO_PIN_CS, LOW);
    delayMicroseconds(4);
    _timoSPI.transfer(0xFF);
    for (uint16_t i = 0; i < count; i++) channels[i] = _timoSPI.transfer(0x00);
    digitalWrite(TIMO_PIN_CS, HIGH);
    _timoSPI.endTransaction();

    return _timoWaitIRQ(true);
}

// ─────────────────────────────────────────────────────────────
//  Higher-level helpers
// ─────────────────────────────────────────────────────────────

// Returns true if the PRODUCT_ID register matches a known TimoTwo
bool timoDetect() {
    uint8_t id[4] = {};
    if (!timoRead(TIMO_REG_PRODUCT_ID, id, 4)) return false;
    return (id[0] == TIMO_PRODUCT_ID_0 && id[1] == TIMO_PRODUCT_ID_1);
}

// Print hardware and firmware version to Serial
void timoPrintVersion() {
    uint8_t v[8] = {};
    if (!timoRead(TIMO_REG_VERSION, v, 8)) {
        Serial.println("[TimoTwo] version read failed");
        return;
    }
    Serial.printf("[TimoTwo] HW: %02X%02X%02X%02X  FW: %d.%d.%d.%d\n",
        v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
}

// Configure TX mode (call before timoEnableRadio)
// protocol: TIMO_RF_CRMX / TIMO_RF_WDMX_G3 / TIMO_RF_WDMX_G4S
// channels: 1–512
// refreshUs: refresh period in µs (25000 = 40 Hz)
bool timoConfigTX(uint8_t protocol, uint16_t channels, uint32_t refreshUs = 25000) {
    // CONFIG: enable radio, TX mode, SPI DMX, UART off
    uint8_t cfg = TIMO_CONFIG_RADIO_EN | TIMO_CONFIG_TX_MODE;
    if (!timoWrite8(TIMO_REG_CONFIG, cfg)) return false;
    delay(200);  // module may restart on mode change

    if (!timoWrite8(TIMO_REG_RF_PROTOCOL, protocol)) return false;
    if (!timoWrite8(TIMO_REG_RF_POWER, TIMO_PWR_20DBM)) return false;

    // DMX_SPEC: channel count + refresh period
    uint8_t spec[8] = {
        (uint8_t)(channels >> 8), (uint8_t)(channels & 0xFF),  // N_CHANNELS
        0x00, 0x00,                                              // interslot time
        (uint8_t)(refreshUs >> 24), (uint8_t)(refreshUs >> 16),
        (uint8_t)(refreshUs >> 8),  (uint8_t)(refreshUs)        // refresh period
    };
    if (!timoWrite(TIMO_REG_DMX_SPEC, spec, 8)) return false;
    if (!timoWrite8(TIMO_REG_DMX_CONTROL, 0x01)) return false;  // enable DMX gen

    return true;
}

// Configure RX mode
bool timoConfigRX() {
    uint8_t cfg = TIMO_CONFIG_RADIO_EN;  // TX_MODE bit = 0 → RX
    if (!timoWrite8(TIMO_REG_CONFIG, cfg)) return false;
    delay(200);
    return true;
}

// Enable radio
bool timoEnableRadio(bool enable = true) {
    uint8_t cfg = timoRead8(TIMO_REG_CONFIG);
    if (enable) cfg |=  TIMO_CONFIG_RADIO_EN;
    else        cfg &= ~TIMO_CONFIG_RADIO_EN;
    return timoWrite8(TIMO_REG_CONFIG, cfg);
}

// Set device name (shown in BLE, RDM DEVICE_LABEL, TX universe name)
// name: null-terminated, max 16 usable chars (32-byte field, null-padded)
bool timoSetDeviceName(const char* name) {
    uint8_t buf[32] = {};
    strncpy((char*)buf, name, 31);
    return timoWrite(TIMO_REG_DEVICE_NAME, buf, 32);
}

// Set OEM info: modelId (2 bytes), manufacturerId ESTA (2 bytes)
// Example: timoSetOEMInfo(0x0001, 0x08B6)  — model 1, Radical Wireless
bool timoSetOEMInfo(uint16_t modelId, uint16_t estaId) {
    uint8_t buf[4] = {
        (uint8_t)(modelId >> 8), (uint8_t)(modelId & 0xFF),
        (uint8_t)(estaId  >> 8), (uint8_t)(estaId  & 0xFF)
    };
    return timoWrite(TIMO_REG_OEM_INFO, buf, 4);
}

// Set universe color (RGB, 3 bytes)
bool timoSetUniverseColor(uint8_t r, uint8_t g, uint8_t b) {
    uint8_t buf[3] = {r, g, b};
    return timoWrite(TIMO_REG_UNIVERSE_COLOR, buf, 3);
}

// Returns true if module is linked to a transmitter (RX) or has active link (TX)
bool timoIsLinked() {
    return (timoRead8(TIMO_REG_STATUS) & TIMO_STATUS_LINKED) != 0;
}

// Returns true if RF link is active
bool timoHasRFLink() {
    return (timoRead8(TIMO_REG_STATUS) & TIMO_STATUS_RF_LINK) != 0;
}

// Returns link quality 0–255 (RX only)
uint8_t timoLinkQuality() {
    return timoRead8(TIMO_REG_LINK_QUALITY);
}

// Returns IRQ_FLAGS byte (check Bit7 for SPI_BUSY, others for events)
uint8_t timoIRQFlags() {
    return timoRead8(TIMO_REG_IRQ_FLAGS);
}

// Check if a specific installed option is present
// knownOptions: 0x2001 = RDM TX via SPI, 0x2002 = RDM TX via proxy
bool timoHasOption(uint16_t optionCode) {
    uint8_t buf[13] = {};
    if (!timoRead(TIMO_REG_INSTALLED_OPT, buf, 13)) return false;
    uint8_t count = buf[0];
    for (uint8_t i = 0; i < count && i < 6; i++) {
        uint16_t opt = ((uint16_t)buf[1 + i*2] << 8) | buf[2 + i*2];
        if (opt == optionCode) return true;
    }
    return false;
}

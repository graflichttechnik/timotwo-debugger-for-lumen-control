/**
 * ╔════════════════════════════════════════════════════════════════╗
 *  TimoTwo ESP32 Web SPI Debugger
 *  Radical Wireless LUMEN CONTROL | ESP32-WROOM-32 dev boards
 * ╠════════════════════════════════════════════════════════════════╣
 *  WiFi: station mode, 2.4 GHz, add credentials in secrets.h
 *  Web UI:            http://timotwo-debug.local  (mDNS)
 * ╠════════════════════════════════════════════════════════════════╣
 *  Author: Stefan Graf - stefan@graf-lichttechnik.de
 *  Version: 1.0.0 - October 2026
 *  Tested with: LumenRadio TimoTwo FX firmware 1.1.1.5
 * ╠════════════════════════════════════════════════════════════════╣
 *  Wiring (LUMEN CONTROL 10-pin header):
 *    Pin  1  VDD  → 3.3 to 10 VDC
 *    Pin  2  CS   → GPIO  5
 *    Pin  3  DMX+ → XLR Pin 3 (A) + 120 Ω terminator
 *    Pin  4  SCK  → GPIO 18
 *    Pin  5  DMX- → XLR Pin 2 (B) + 120 Ω terminator
 *    Pin  6  MOSI → GPIO 23
 *    Pin  7  IRQ  → GPIO  4  (input, pull-up)
 *    Pin  8  MISO → GPIO 19
 *    Pin  9  LINK → GPIO  2  (optional, input pull-up)
 *    Pin 10  VSS  → GND
 * ╚════════════════════════════════════════════════════════════════╝
 */

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include "secrets.h"   // defines WIFI_SSID and WIFI_PASS

#define SKETCH_VERSION "1.0.0"

// ── Pins (ESP32-WROOM-32, VSPI) ───────────────────────────────
#define PIN_CS     5   // LUMEN header pin 2
#define PIN_MOSI  23   // LUMEN header pin 6
#define PIN_MISO  19   // LUMEN header pin 8
#define PIN_SCK   18   // LUMEN header pin 4
#define PIN_IRQ    4   // LUMEN header pin 7
#define PIN_LINK   2   // LUMEN header pin 9 (optional)

// ── WiFi: connects to an existing network ─────────────────────
const char* MDNS_NAME = "timotwo-debug";   // reachable at http://timotwo-debug.local
#define WIFI_CONNECT_TIMEOUT_MS 20000UL

// ── SPI ───────────────────────────────────────────────────────
SPIClass timoSPI(VSPI);
#define SPI_FREQ         1000000UL   // 1 MHz (Dev Doc maximum: 8 MHz)
#define IRQ_TIMEOUT_MS   200
#define SPI_BUSY_BUDGET_MS 30        // a command sequence is restarted for this long while the module reports busy
#define RESTART_WAIT_MS  300         // wait after a write that restarts the module
#define RESTART_POLL_MS  3000        // then poll PRODUCT_ID for up to this long
#define LABEL_SETTLE_MS  2000        // wait after DEVICE_NAME / OEM_INFO writes

WebServer server(80);

// ── DMX buffer (mirrors last TX state) ────────────────────────
static uint8_t dmxBuf[512] = {};  // TX: last written universe

// ════════════════════════════════════════════════════════════════
//  Register table (29 registers of the Dev Doc register map)
//  Undefined addresses are never sent to the module.
// ════════════════════════════════════════════════════════════════

#define ACC_R   1
#define ACC_W   2
#define SC_BOTH 0
#define SC_RX   1
#define SC_TX   2

struct RegDef { uint8_t addr, len, acc, scope; };

static const RegDef REGS[] = {
    {0x00, 1,  ACC_R | ACC_W, SC_BOTH},   // CONFIG
    {0x01, 1,  ACC_R | ACC_W, SC_BOTH},   // STATUS
    {0x02, 1,  ACC_R | ACC_W, SC_BOTH},   // IRQ_MASK
    {0x03, 1,  ACC_R,         SC_BOTH},   // IRQ_FLAGS
    {0x04, 4,  ACC_R | ACC_W, SC_BOTH},   // DMX_WINDOW
    {0x05, 3,  ACC_R,         SC_BOTH},   // ASC_FRAME
    {0x06, 1,  ACC_R,         SC_RX  },   // LINK_QUALITY
    {0x08, 8,  ACC_R | ACC_W, SC_TX  },   // DMX_SPEC
    {0x09, 1,  ACC_R | ACC_W, SC_TX  },   // DMX_CONTROL
    {0x0A, 4,  ACC_R | ACC_W, SC_BOTH},   // EXTENDED_IRQ_MASK
    {0x0B, 4,  ACC_R | ACC_W, SC_BOTH},   // EXTENDED_IRQ_FLAGS
    {0x0C, 1,  ACC_R | ACC_W, SC_TX  },   // RF_PROTOCOL
    {0x0D, 1,  ACC_R,         SC_BOTH},   // DMX_SOURCE
    {0x0E, 1,  ACC_R | ACC_W, SC_BOTH},   // LOLLIPOP
    {0x10, 8,  ACC_R,         SC_BOTH},   // VERSION
    {0x11, 1,  ACC_R | ACC_W, SC_TX  },   // RF_POWER
    {0x12, 11, ACC_R | ACC_W, SC_TX  },   // BLOCKED_CHANNELS
    {0x20, 6,  ACC_R | ACC_W, SC_BOTH},   // BINDING_UID
    {0x21, 10, ACC_W,         SC_BOTH},   // LINKING_KEY (RX 10 bytes, TX 8 bytes)
    {0x30, 1,  ACC_R | ACC_W, SC_BOTH},   // BLE_STATUS
    {0x31, 6,  ACC_W,         SC_BOTH},   // BLE_PIN
    {0x32, 1,  ACC_W,         SC_BOTH},   // BATTERY
    {0x33, 3,  ACC_R | ACC_W, SC_BOTH},   // UNIVERSE_COLOR (write: TX only)
    {0x34, 4,  ACC_R | ACC_W, SC_BOTH},   // OEM_INFO
    {0x35, 1,  ACC_R,         SC_BOTH},   // RXTX_STATUS
    {0x36, 32, ACC_R | ACC_W, SC_BOTH},   // DEVICE_NAME
    {0x37, 16, ACC_R,         SC_RX  },   // UNIVERSE_NAME
    {0x3D, 13, ACC_R,         SC_BOTH},   // INSTALLED_OPTIONS
    {0x3F, 4,  ACC_R,         SC_BOTH},   // PRODUCT_ID
};
static const int NREGS = sizeof(REGS) / sizeof(REGS[0]);

// Index of a register in REGS, -1 if the address is not in the register map
static int regIndex(uint8_t addr) {
    for (int i = 0; i < NREGS; i++) if (REGS[i].addr == addr) return i;
    return -1;
}

// Writes to these registers can restart the module (Dev Doc register map)
static bool isRestartReg(uint8_t addr) {
    return addr == 0x00 || addr == 0x0C || addr == 0x21 || addr == 0x30 || addr == 0x31;
}

// ════════════════════════════════════════════════════════════════
//  SPI low-level
// ════════════════════════════════════════════════════════════════

// Wait for IRQ pin to reach targetHigh state, with timeout
static bool waitIRQ(bool targetHigh, uint32_t ms = IRQ_TIMEOUT_MS) {
    const uint32_t t = millis();
    while ((bool)digitalRead(PIN_IRQ) != targetHigh) {
        if (millis() - t > ms) return false;
        delayMicroseconds(10);
    }
    return true;
}

// One complete command sequence: command byte, IRQ low (acknowledged),
// payload (first byte ignored by the module), IRQ high (processed).
// IRQ_FLAGS bit 7 (SPI_DEVICE_BUSY) set in the first byte restarts the whole sequence,
// with a short pause, for up to SPI_BUSY_BUDGET_MS.
// Write: wbuf is sent. Read: rbuf receives len bytes.
static bool spiCommand(uint8_t cmd, const uint8_t* wbuf, uint8_t* rbuf, uint16_t len) {
    const uint32_t t0 = millis();
    for (;;) {
        timoSPI.beginTransaction(SPISettings(SPI_FREQ, MSBFIRST, SPI_MODE0));
        digitalWrite(PIN_CS, LOW);
        delayMicroseconds(4);
        uint8_t flags = timoSPI.transfer(cmd);
        digitalWrite(PIN_CS, HIGH);
        timoSPI.endTransaction();
        if (!(flags & 0x80)) break;              // not busy
        if (millis() - t0 > SPI_BUSY_BUDGET_MS) return false;   // stayed busy
        waitIRQ(true, 5);
        delayMicroseconds(500);
    }
    if (!waitIRQ(false)) return false;           // IRQ LOW: command acknowledged

    timoSPI.beginTransaction(SPISettings(SPI_FREQ, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_CS, LOW);
    delayMicroseconds(4);
    timoSPI.transfer(0xFF);                      // first byte is ignored by the module
    if (wbuf) for (uint16_t i = 0; i < len; i++) timoSPI.transfer(wbuf[i]);
    else      for (uint16_t i = 0; i < len; i++) rbuf[i] = timoSPI.transfer(0x00);
    digitalWrite(PIN_CS, HIGH);
    timoSPI.endTransaction();

    return waitIRQ(true);                        // IRQ HIGH: processing complete
}

// Read `len` bytes from register at `addr`
bool spiReadReg(uint8_t addr, uint8_t* buf, uint8_t len) {
    if (!buf || len == 0 || len > 64) return false;
    return spiCommand(addr & 0x3F, nullptr, buf, len);       // READ_REG: 00AA AAAA
}

// Write `len` bytes to register at `addr`
bool spiWriteReg(uint8_t addr, const uint8_t* buf, uint8_t len) {
    if (!buf || len == 0 || len > 64) return false;
    bool ok = spiCommand(0x40 | (addr & 0x3F), buf, nullptr, len);   // WRITE_REG: 01AA AAAA
    if (ok) {
        Serial.printf("[SPI] WRITE 0x%02X OK (%d B):", addr, len);
        for (uint8_t i = 0; i < len; i++) Serial.printf(" %02X", buf[i]);
        Serial.println();
    } else {
        Serial.printf("[SPI] WRITE 0x%02X FAILED\n", addr);
    }
    return ok;
}

// After a write that restarts the module: wait, then poll PRODUCT_ID (F1 40) until it answers
static bool waitModuleReady() {
    delay(RESTART_WAIT_MS);
    const uint32_t t0 = millis();
    while (millis() - t0 < RESTART_POLL_MS) {
        uint8_t id[4] = {};
        if (spiReadReg(0x3F, id, 4) && id[0] == 0xF1 && id[1] == 0x40) return true;
        delay(50);
        yield();
    }
    return false;
}

// Operating mode from CONFIG bit 1 (0 = RX, 1 = TX)
static bool readMode(bool &tx) {
    uint8_t c = 0;
    if (!spiReadReg(0x00, &c, 1)) return false;
    tx = (c & 0x02) != 0;
    return true;
}

// ════════════════════════════════════════════════════════════════
//  Request parsing and JSON helpers
// ════════════════════════════════════════════════════════════════

static void sendErr(int code, const char* msg) {
    String j = "{\"ok\":false,\"error\":\"";
    j += msg;
    j += "\"}";
    server.send(code, "application/json", j);
}

static String addrKey(uint8_t a) {
    char b[8];
    snprintf(b, sizeof(b), "0x%02X", a);
    return String(b);
}

static void appendBytes(String &j, const uint8_t* b, uint16_t n) {
    for (uint16_t i = 0; i < n; i++) { j += b[i]; if (i + 1 < n) j += ','; }
}

// Decimal digits, or 0x / 0X followed by hex digits. No sign, no octal.
static bool parseNumber(const String &s, uint32_t maxVal, uint32_t &out) {
    String t = s;
    t.trim();
    if (t.length() == 0) return false;
    bool hex = t.length() > 2 && t.charAt(0) == '0' && (t.charAt(1) == 'x' || t.charAt(1) == 'X');
    const char* p = t.c_str() + (hex ? 2 : 0);
    if (*p == 0) return false;
    uint32_t v = 0;
    for (; *p; p++) {
        int d;
        if (*p >= '0' && *p <= '9') d = *p - '0';
        else if (hex && *p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
        else if (hex && *p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
        else return false;
        v = v * (hex ? 16 : 10) + (uint32_t)d;
        if (v > maxVal) return false;
    }
    out = v;
    return true;
}

// Hex byte pairs, optional leading 0x, spaces between bytes allowed.
// Returns the byte count, or: -1 non-hex character, -2 odd digit count,
// -3 empty, -4 more than maxLen bytes.
static int parseHexBytes(const String &in, uint8_t* out, uint16_t maxLen) {
    String t = in;
    t.trim();
    if (t.length() >= 2 && t.charAt(0) == '0' && (t.charAt(1) == 'x' || t.charAt(1) == 'X'))
        t = t.substring(2);
    uint16_t digits = 0;
    int nib = -1;
    uint16_t n = 0;
    for (uint16_t i = 0; i < t.length(); i++) {
        char c = t.charAt(i);
        if (c == ' ') continue;
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        digits++;
        if (nib < 0) { nib = d; }
        else {
            if (n >= maxLen) return -4;
            out[n++] = (uint8_t)((nib << 4) | d);
            nib = -1;
        }
    }
    if (digits == 0) return -3;
    if (nib >= 0) return -2;
    return n;
}

static const char* hexErrText(int code) {
    switch (code) {
        case -1: return "data contains a non-hex character";
        case -2: return "data must be an even number of hex digits";
        case -3: return "data is empty";
        default: return "data is longer than the maximum length";
    }
}

// ════════════════════════════════════════════════════════════════
//  HTTP handler: GET /api/read?addr=XX[&len=N]
//  len, when given, must equal the register length.
// ════════════════════════════════════════════════════════════════

void handleRead() {
    uint32_t a = 0;
    if (!server.hasArg("addr") || !parseNumber(server.arg("addr"), 0xFF, a)) {
        sendErr(400, "addr required (decimal or 0x hex)");
        return;
    }
    int idx = regIndex((uint8_t)a);
    if (idx < 0) { sendErr(400, "address is not in the register map"); return; }
    if (!(REGS[idx].acc & ACC_R)) { sendErr(400, "register is write-only"); return; }
    if (server.hasArg("len")) {
        uint32_t l = 0;
        if (!parseNumber(server.arg("len"), 255, l) || l != REGS[idx].len) {
            sendErr(400, "len must equal the register length");
            return;
        }
    }
    uint8_t len = REGS[idx].len;
    uint8_t buf[64] = {};
    if (!spiReadReg((uint8_t)a, buf, len)) { sendErr(200, "SPI timeout"); return; }
    String j = "{\"ok\":true,\"addr\":\"" + addrKey((uint8_t)a) + "\",\"bytes\":[";
    appendBytes(j, buf, len);
    j += "]}";
    server.send(200, "application/json", j);
}

// ════════════════════════════════════════════════════════════════
//  HTTP handler: POST /api/write   addr=XX&data=AABBCC (form body or query string)
//  data must have exactly the register length (LINKING_KEY: 10 bytes in RX, 8 in TX).
//  Writes that restart the module are followed by a wait for the module to answer again.
//  DEVICE_NAME and OEM_INFO writes are followed by a 2 s pause.
// ════════════════════════════════════════════════════════════════

void handleWrite() {
    uint32_t a = 0;
    if (!server.hasArg("addr") || !parseNumber(server.arg("addr"), 0xFF, a)) {
        sendErr(400, "addr required (decimal or 0x hex)");
        return;
    }
    if (!server.hasArg("data")) { sendErr(400, "data required"); return; }
    int idx = regIndex((uint8_t)a);
    if (idx < 0) { sendErr(400, "address is not in the register map"); return; }
    if (!(REGS[idx].acc & ACC_W)) { sendErr(400, "register is read-only"); return; }
    uint8_t addr = (uint8_t)a;

    uint8_t buf[64] = {};
    int n = parseHexBytes(server.arg("data"), buf, 64);
    if (n < 0) { sendErr(400, hexErrText(n)); return; }

    uint8_t want = REGS[idx].len;
    if (addr == 0x21 || addr == 0x33) {
        bool tx = false;
        if (!readMode(tx)) { sendErr(200, "SPI timeout"); return; }
        if (addr == 0x21) want = tx ? 8 : 10;
        if (addr == 0x33 && !tx) { sendErr(400, "UNIVERSE_COLOR is read-only in RX mode"); return; }
    }
    if (n != want) { sendErr(400, "data length must equal the register length"); return; }

    bool prevTx = false, haveMode = false;
    if (addr == 0x00) haveMode = readMode(prevTx);

    bool ok = spiWriteReg(addr, buf, (uint8_t)n);
    String extra;
    if (isRestartReg(addr)) {
        bool ready = waitModuleReady();
        extra += ",\"restart\":true,\"ready\":";
        extra += ready ? "true" : "false";
        if (addr == 0x00 && !ok && ready && haveMode) {
            // A mode change restarts the module and can swallow the write acknowledge.
            // CONFIG readback decides the result.
            uint8_t rb = 0;
            if (spiReadReg(0x00, &rb, 1)) {
                extra += ",\"readback\":";
                extra += rb;
                bool newTx = (buf[0] & 0x02) != 0;
                if (newTx != prevTx && ((rb & 0x02) != 0) == newTx) { ok = true; extra += ",\"ack_lost\":true"; }
            }
        }
    } else if (ok && (addr == 0x34 || addr == 0x36)) {
        delay(LABEL_SETTLE_MS);
    }
    if (!ok) { sendErr(200, "SPI timeout"); return; }
    server.send(200, "application/json", "{\"ok\":true" + extra + "}");
}

// ════════════════════════════════════════════════════════════════
//  HTTP handler: GET /api/readall
//  Keys are hex addresses ("0x34"). Registers that exist only in the other
//  operating mode are reported as skipped ("tx_only" / "rx_only").
// ════════════════════════════════════════════════════════════════

void handleReadAll() {
    bool tx = false;
    if (!readMode(tx)) { sendErr(200, "SPI timeout"); return; }
    String j = "{\"ok\":true,\"mode\":\"";
    j += tx ? "tx" : "rx";
    j += "\",\"registers\":{";
    bool first = true;
    for (int i = 0; i < NREGS; i++) {
        if (!(REGS[i].acc & ACC_R)) continue;
        if (!first) j += ',';
        first = false;
        j += '"'; j += addrKey(REGS[i].addr); j += "\":{";
        if ((REGS[i].scope == SC_TX && !tx) || (REGS[i].scope == SC_RX && tx)) {
            j += "\"ok\":false,\"skipped\":\"";
            j += (REGS[i].scope == SC_TX) ? "tx_only" : "rx_only";
            j += "\"}";
            continue;
        }
        uint8_t buf[64] = {};
        bool ok = spiReadReg(REGS[i].addr, buf, REGS[i].len);
        j += "\"ok\":";
        j += ok ? "true" : "false";
        j += ",\"bytes\":[";
        if (ok) appendBytes(j, buf, REGS[i].len);
        j += "]}";
        yield();
    }
    j += "}}";
    server.send(200, "application/json", j);
}

// ════════════════════════════════════════════════════════════════
//  DMX SPI data functions
//  Firmware 1.1.1.0 and later accept DMX in blocks of up to 512 bytes
//  (Release Notes). One block per transfer, no other SPI command in between.
// ════════════════════════════════════════════════════════════════

// Write `count` channels (1..512) with one WRITE_DMX block
bool timoWriteDMXBlock(const uint8_t* ch, uint16_t count) {
    if (!ch || count == 0 || count > 512) return false;
    bool ok = spiCommand(0x91, ch, nullptr, count);   // WRITE_DMX
    Serial.printf(ok ? "[DMX] WRITE %d ch OK\n" : "[DMX] WRITE %d ch FAILED\n", count);
    return ok;
}

// Set DMX_WINDOW (size, start), then read the window with one READ_DMX block.
// No other SPI command may sit between the window write and READ_DMX.
bool timoReadDMXWindow(uint8_t* buf, uint16_t start, uint16_t count) {
    if (!buf || count == 0 || start >= 512 || start + count > 512) return false;
    // DMX_WINDOW: bytes 0-1 = window size, bytes 2-3 = start address (big-endian)
    uint8_t win[4] = {
        (uint8_t)(count >> 8), (uint8_t)(count & 0xFF),
        (uint8_t)(start >> 8), (uint8_t)(start & 0xFF)
    };
    if (!spiWriteReg(0x04, win, 4)) {
        Serial.println("[DMX] READ: DMX_WINDOW set FAILED");
        return false;
    }
    bool ok = spiCommand(0x81, nullptr, buf, count);  // READ_DMX
    Serial.printf(ok ? "[DMX] READ %d ch from ch %d OK\n" : "[DMX] READ %d ch from ch %d FAILED\n",
                  count, start + 1);
    return ok;
}

// ════════════════════════════════════════════════════════════════
//  HTTP handler: POST /api/dmx/write
//  Body (application/x-www-form-urlencoded): data=<hex string, 1..512 bytes>
//  Always writes 512 channels (unused channels are padded with 0).
// ════════════════════════════════════════════════════════════════

void handleDmxWrite() {
    if (!server.hasArg("data")) { sendErr(400, "data required"); return; }
    static uint8_t parsed[512];
    int n = parseHexBytes(server.arg("data"), parsed, 512);
    if (n < 0) { sendErr(400, hexErrText(n)); return; }
    memcpy(dmxBuf, parsed, (size_t)n);
    for (uint16_t i = (uint16_t)n; i < 512; i++) dmxBuf[i] = 0;   // pad unused channels

    bool ok = timoWriteDMXBlock(dmxBuf, 512);
    if (!ok) { sendErr(200, "SPI timeout"); return; }
    server.send(200, "application/json", "{\"ok\":true}");
}

// ════════════════════════════════════════════════════════════════
//  HTTP handler: GET /api/dmx/read?start=N&count=M
//  start: 0-indexed channel offset (default 0), 0..511
//  count: number of channels (default 64), 1..512, start + count <= 512
// ════════════════════════════════════════════════════════════════

void handleDmxRead() {
    uint32_t start = 0, count = 64;
    if (server.hasArg("start") && !parseNumber(server.arg("start"), 511, start)) {
        sendErr(400, "start must be 0 to 511");
        return;
    }
    if (server.hasArg("count") && (!parseNumber(server.arg("count"), 512, count) || count < 1)) {
        sendErr(400, "count must be 1 to 512");
        return;
    }
    if (start + count > 512) { sendErr(400, "start + count must not exceed 512"); return; }

    static uint8_t rxBuf[512];
    if (!timoReadDMXWindow(rxBuf, (uint16_t)start, (uint16_t)count)) {
        sendErr(200, "SPI read failed");
        return;
    }
    String j = "{\"ok\":true,\"start\":";
    j += start;
    j += ",\"count\":";
    j += count;
    j += ",\"data\":[";
    for (uint16_t i = 0; i < count; i++) {
        j += rxBuf[i];
        if (i + 1 < count) j += ',';
        if (i % 64 == 63) yield();
    }
    j += "]}";
    server.send(200, "application/json", j);
}

// ════════════════════════════════════════════════════════════════
//  Web UI
// ════════════════════════════════════════════════════════════════

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang='en'>
<head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1.0, minimum-scale=1.0'>
<title>TimoTwo SPI Debugger for LUMEN CONTROL module</title>
<style>
:root {
  --bg:       #f5f5f7;
  --surf:     #ffffff;
  --surf2:    #f9f9fb;
  --cat-bg:   #eaf2ff;
  --cat-line: #007aff;
  --border:   #d2d2d7;
  --bord2:    #e5e5ea;
  --text:     #1d1d1f;
  --secondary:#6e6e73;
  --tertiary: #8e8e93;
  --accent:   #007aff;
  --green:    #28cd41;
  --orange:   #e67e00;
  --red:      #ff3b30;

  /* Pastel Read (green) */
  --r-bg:    #d4f5e2;
  --r-text:  #1a6e38;
  --r-bord:  #7ec89a;
  --r-hover: #b4e8c8;

  /* Pastel Write (orange) */
  --w-bg:    #fde8c8;
  --w-text:  #a84e00;
  --w-bord:  #e8a060;
  --w-hover: #f8d0a0;

  --sans: -apple-system, BlinkMacSystemFont, 'SF Pro Text', 'Helvetica Neue', Arial, sans-serif;
  --mono: 'SF Mono', 'Cascadia Code', 'Fira Code', Consolas, monospace;

  --fs-title: clamp(15px, 1.3vw, 18px);
  --fs-base:  clamp(13px, 1.05vw, 15px);
  --fs-sm:    clamp(12px, 0.95vw, 14px);
  --fs-xs:    clamp(11px, 0.85vw, 12px);

  /* Register table columns: fixed widths, Description takes the rest.
     --w-desc is the width of the longest description line, it only sets the maximum canvas width. */
  --w-addr: 66px;  --w-name: 200px; --w-type: 56px; --w-len: 48px;
  --w-desc: 580px; --w-val: 280px;  --w-act: 350px;
  --w-desc-min: 220px;
  --w-fixed: calc(var(--w-addr) + var(--w-name) + var(--w-type) + var(--w-len) + var(--w-val) + var(--w-act));
  --canvas-max: calc(var(--w-fixed) + var(--w-desc));
  --radius:   10px;
  --radius-sm:6px;
}

* { box-sizing: border-box; margin: 0; padding: 0; }
html { scroll-behavior: smooth; }
body {
  background: var(--bg);
  color: var(--text);
  font-family: var(--sans);
  font-size: var(--fs-base);
  line-height: 1.5;
  padding-bottom: 54px;
  overflow-x: auto;
  -webkit-overflow-scrolling: touch;
}

/* ── Page header ── */
header {
  position: sticky; top: 0; z-index: 200;
  display: flex; align-items: center; justify-content: space-between;
  padding: 14px 24px;
  background: rgba(255,255,255,0.88);
  backdrop-filter: blur(16px);
  -webkit-backdrop-filter: blur(16px);
  border-bottom: 1px solid var(--border);
  box-shadow: 0 1px 4px rgba(0,0,0,.06);
}
.hd-title h1 {
  font-size: var(--fs-title);
  font-weight: 600; color: var(--text); letter-spacing: -0.02em;
}
.hd-title p { font-size: var(--fs-xs); color: var(--secondary); margin-top: 2px; }
.hd-ctrl { display: flex; align-items: center; gap: 10px; }

/* ── Status dot ── */
.status { display: flex; align-items: center; gap: 6px; font-size: var(--fs-xs); color: var(--secondary); }
.dot { width: 7px; height: 7px; border-radius: 50%; background: var(--border); flex-shrink: 0; transition: all .3s; }
.dot.ok   { background: var(--green); box-shadow: 0 0 6px rgba(40,205,65,.4); }
.dot.err  { background: var(--red); }
.dot.busy { background: var(--orange); animation: pulse 1s infinite; }
@keyframes pulse { 0%,100%{opacity:1}50%{opacity:.4} }

/* ── Header buttons ── */
.hbtn {
  display: inline-flex; align-items: center; gap: 5px;
  padding: 7px 15px;
  background: var(--surf); border: 1px solid var(--border);
  color: var(--text); font-family: var(--sans); font-size: var(--fs-sm);
  font-weight: 500; cursor: pointer; border-radius: var(--radius-sm);
  transition: all .15s; -webkit-appearance: none;
}
.hbtn:hover { background: #ebebef; }
.hbtn:active { transform: scale(.97); }
.hbtn.accent {
  background: var(--accent); border-color: var(--accent); color: #fff;
}
.hbtn.accent:hover { background: #0066dd; }

/* ── Wrapper / spacing ── */
#canvas { max-width: calc(var(--canvas-max) + 32px); margin: 0 auto; padding: 14px 16px 0; }
#main { display: flex; flex-direction: column; gap: 12px; }

/* ── Category card ── */
.cat {
  background: var(--surf);
  border-radius: var(--radius);
  overflow: hidden;
  box-shadow: 0 1px 4px rgba(0,0,0,.08), 0 0 0 1px var(--bord2);
}
.cat-hd {
  display: flex; align-items: center; gap: 10px;
  padding: 12px 18px;
  background: var(--cat-bg);
  border-left: 4px solid var(--cat-line);
  cursor: pointer; user-select: none; transition: background .12s;
}
.cat-hd:hover { background: #dae8ff; }
.cat-arrow { color: var(--accent); font-size: 10px; width: 12px; transition: transform .2s; flex-shrink: 0; }
.cat-arrow.closed { transform: rotate(-90deg); }
.cat-name {
  font-size: var(--fs-sm); font-weight: 600; color: var(--text);
  letter-spacing: .04em; text-transform: uppercase;
}
.cat-cnt { margin-left: auto; font-size: var(--fs-xs); color: var(--secondary); }
.cat-body.hidden { display: none; }
.cat-body .tbl-wrap {
  overflow-x: auto;
  -webkit-overflow-scrolling: touch;
}

/* ── Register table ── */
/* Same column widths in every register table; below the minimum width the card scrolls sideways */
table { width: 100%; min-width: calc(var(--w-fixed) + var(--w-desc-min)); table-layout: fixed; border-collapse: collapse; }
col.w-addr { width: var(--w-addr); }
col.w-name { width: var(--w-name); }
col.w-type { width: var(--w-type); }
col.w-len  { width: var(--w-len); }
col.w-desc { width: auto; }
col.w-val  { width: var(--w-val); }
col.w-act  { width: var(--w-act); }
thead tr th {
  padding: 8px 12px;
  text-align: left; font-size: var(--fs-xs); font-weight: 600;
  color: var(--secondary); letter-spacing: .07em; text-transform: uppercase;
  background: var(--surf2); border-bottom: 1px solid var(--border);
}
thead tr th:first-child { padding-left: 20px; }
thead tr th:last-child  { padding-right: 20px; }

tbody tr { border-bottom: 1px solid var(--bord2); transition: background .08s; }
tbody tr:last-child { border-bottom: none; }
tbody tr:hover { background: #f0f6ff; }
tbody tr td { padding: 10px 12px; vertical-align: top; }
tbody tr td:first-child { padding-left: 20px; }
tbody tr td:last-child  { padding-right: 20px; }

/* ── Column styles ── */
.c-addr { font-family: var(--mono); color: var(--secondary); font-size: var(--fs-sm); white-space: nowrap; padding-top: 12px !important; }
.c-name { font-family: var(--mono); color: var(--text); font-weight: 700; font-size: var(--fs-base); padding-top: 12px !important; }
.c-type { text-align: center; padding-top: 12px !important; }
.c-len  { color: var(--secondary); font-size: var(--fs-sm); text-align: center; padding-top: 12px !important; }
.c-desc { color: var(--secondary); font-size: var(--fs-sm); line-height: 1.55; padding-top: 11px !important; }
.c-ctrl { white-space: nowrap; padding-top: 9px !important; }

/* ── Type badges ── */
.badge {
  display: inline-block; padding: 3px 7px; border-radius: 5px;
  font-family: var(--mono); font-size: var(--fs-xs); font-weight: 700;
  letter-spacing: .04em; min-width: 36px; text-align: center;
}
.b-r  { background: var(--r-bg);  color: var(--text); border: 1px solid var(--r-bord); }
.b-rw { background: #e5f0ff;      color: var(--text); border: 1px solid #9dbdee; }
.b-w  { background: var(--w-bg);  color: var(--text); border: 1px solid var(--w-bord); }

/* ── Hex value boxes ── */
.vwrap { display: flex; flex-wrap: wrap; gap: 3px; margin-top: 6px; }
.bx {
  display: inline-block; padding: 3px 8px; min-width: 32px;
  background: var(--surf2); border: 1px solid var(--border);
  border-radius: 4px; font-family: var(--mono); font-size: var(--fs-sm);
  color: var(--text); text-align: center; transition: all .35s;
}
.bx.live { background: #e4f7ec; border-color: #7ec89a; color: var(--text); }
.bx.err  { background: #fff2f2; border-color: #ffaaaa; color: var(--text); }

/* ── Decoded value text ── */
.vdec {
  margin-top: 5px;
  font-size: var(--fs-xs);
  color: var(--secondary);
  font-style: italic;
  line-height: 1.45;
  word-break: break-word;
}
.vdec:empty { display: none; }

/* ── Action buttons ── */
.ctrl { display: flex; align-items: flex-start; gap: 5px; flex-wrap: nowrap; padding-top: 4px; }
.rbtn, .wbtn {
  display: inline-flex; align-items: center;
  padding: 5px 11px; border-radius: var(--radius-sm);
  font-family: var(--mono); font-size: var(--fs-xs); font-weight: 700;
  cursor: pointer; border: 1px solid; transition: all .15s;
  -webkit-appearance: none; letter-spacing: .03em;
}
.rbtn { background: var(--r-bg); color: var(--text); border-color: var(--r-bord); }
.rbtn:hover { background: var(--r-hover); border-color: var(--r-text); }
.wbtn { background: var(--w-bg); color: var(--text); border-color: var(--w-bord); }
.wbtn:hover { background: var(--w-hover); border-color: var(--w-text); }
.rbtn:active, .wbtn:active { transform: scale(.95); }

/* ── Hex input field ── */
input.hex {
  background: var(--surf); border: 1px solid var(--border);
  color: var(--text); font-family: var(--mono); font-size: var(--fs-sm);
  padding: 5px 9px; border-radius: var(--radius-sm); outline: none;
  transition: border-color .15s, box-shadow .15s;
}
input.hex:focus {
  border-color: var(--accent);
  box-shadow: 0 0 0 3px rgba(0,122,255,.15);
}
input.hex::placeholder { color: var(--tertiary); }

.msg { font-size: var(--fs-sm); min-width: 14px; font-weight: 600; margin-left: 2px; }
.msg.ok  { color: #1a6e38; }
.msg.err { color: var(--red); }

/* ── Log bar ── */
#logbar {
  position: fixed; bottom: 0; left: 0; right: 0;
  display: flex; align-items: center; gap: 16px;
  padding: 9px 24px;
  background: rgba(255,255,255,.95);
  backdrop-filter: blur(10px);
  -webkit-backdrop-filter: blur(10px);
  border-top: 1px solid var(--border);
  font-size: var(--fs-sm);
  box-shadow: 0 -1px 4px rgba(0,0,0,.06);
}
#logmsg { color: var(--secondary); flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
#logmsg.ok  { color: #1a6e38; font-weight: 500; }
#logmsg.err { color: var(--red); font-weight: 500; }
.log-meta { color: var(--tertiary); flex-shrink: 0; font-size: var(--fs-xs); font-family: var(--mono); }
/* ── DMX test panel ── */
#dmx-panel{
  margin:0 0 12px;background:var(--surf);border-radius:var(--radius);overflow:hidden;
  box-shadow:0 1px 4px rgba(0,0,0,.08), 0 0 0 1px var(--bord2);
}
.dmx-hd{
  display:flex;align-items:center;flex-wrap:wrap;gap:6px 10px;padding:12px 18px;
  background:var(--cat-bg);border-left:4px solid var(--cat-line);
  cursor:pointer;user-select:none;transition:background .1s;
}
.dmx-hd:hover{background:#d8eaff}
.dmx-arrow{color:var(--cat-line);font-size:10px;transition:transform .2s}
.dmx-arrow.closed{transform:rotate(-90deg)}
.dmx-title{font-size:var(--fs-sm);font-weight:600;color:var(--text);letter-spacing:.04em;text-transform:uppercase}
.dmx-hint{margin-left:auto;text-align:right;font-size:var(--fs-xs);color:var(--secondary);font-family:var(--mono)}
.dmx-body{display:grid;grid-template-columns:1fr 1fr;border-top:1px solid var(--bord2)}
.dmx-body.collapsed{display:none}
@media(max-width:720px){.dmx-body{grid-template-columns:1fr}}
.dmx-sect{padding:16px 20px;border-right:1px solid var(--bord2)}
.dmx-sect:last-child{border-right:none}
.dmx-sect-title{font-size:var(--fs-xs);font-weight:700;color:var(--secondary);text-transform:uppercase;letter-spacing:.07em;margin-bottom:10px}
.dmx-row{display:flex;gap:6px;flex-wrap:wrap;align-items:center;margin-bottom:8px}
.dmx-row label{font-size:var(--fs-xs);color:var(--secondary);white-space:nowrap}
.dmx-num{width:52px;padding:4px 6px;border:1px solid var(--border);border-radius:var(--radius-sm);background:var(--surf);color:var(--text);font-family:var(--mono);font-size:var(--fs-xs);text-align:center}
.dmx-num:focus{outline:none;border-color:var(--accent);box-shadow:0 0 0 3px rgba(0,122,255,.12)}
.dmx-slider{flex:1;accent-color:var(--accent);min-width:60px}
.dbtn{
  padding:5px 12px;border:1px solid var(--border);background:var(--surf);
  color:var(--text);font-family:var(--sans);font-size:var(--fs-xs);font-weight:500;
  border-radius:var(--radius-sm);cursor:pointer;transition:all .15s;white-space:nowrap;
}
.dbtn:hover{border-color:var(--accent);color:var(--accent)}
.dbtn:active{transform:scale(.97)}
.dbtn.primary{background:var(--accent);color:#fff;border-color:var(--accent)}
.dbtn.primary:hover{background:#005ec2}
.dbtn.black{background:#1d1d1f;color:#fff;border-color:#1d1d1f}
.dbtn.green{background:var(--r-bg);color:var(--text);border-color:var(--r-bord)}
.dbtn.on{background:var(--accent);color:#fff;border-color:var(--accent)}
/* Channel grid */
.dmx-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(48px,1fr));gap:3px;margin-top:8px}
.dmx-cell{
  background:var(--surf2);border:1px solid var(--bord2);border-radius:4px;
  padding:4px 3px 3px;text-align:center;cursor:default;transition:border-color .15s;
}
.dmx-cell:hover{border-color:var(--accent)}
.dmx-cell.clickable{cursor:pointer}
.dmx-ch{display:block;font-size:9px;color:var(--tertiary);font-family:var(--mono);margin-bottom:2px}
.dmx-bar-wrap{background:var(--bord2);border-radius:2px;height:5px;margin-bottom:3px;overflow:hidden}
.dmx-bar{height:100%;background:var(--accent);border-radius:2px;transition:width .12s}
.dmx-val{display:block;font-size:10px;font-weight:600;font-family:var(--mono);color:var(--text)}
.dmx-pct{font-family:var(--mono);font-size:var(--fs-sm);color:var(--secondary);min-width:52px}
.dmx-pg{font-family:var(--mono);font-size:var(--fs-xs);color:var(--secondary);min-width:96px;text-align:center}
/* ── Register tags ── */
.tags{margin-top:5px}
.tag{display:inline-block;margin:0 6px 0 0;padding:1px 6px;border-radius:4px;border:1px solid var(--border);
  background:var(--surf2);color:var(--text);font-family:var(--sans);font-size:var(--fs-xs);font-weight:600;
  letter-spacing:.03em;white-space:nowrap}
.tag.rst{background:var(--w-bg);color:var(--text);border-color:var(--w-bord);cursor:help}
</style>
</head>
<body>

<header>
  <div class='hd-title'>
    <h1>TimoTwo SPI Debugger</h1>
    <p>Radical Wireless LUMEN CONTROL &nbsp;·&nbsp; LumenRadio TimoTwo FX</p>
  </div>
  <div class='hd-ctrl'>
    <div class='status'>
      <div class='dot' id='dot'></div>
      <span id='stxt'>Idle</span>
    </div>
    <button class='hbtn accent' onclick='readAll()'>▶&nbsp; Read all</button>
    <button class='hbtn' onclick='clearVals()'>✕&nbsp; Clear</button>
  </div>
</header>

<div id='canvas'>
<div id='dmx-panel'>
  <div class='dmx-hd' onclick='dmxToggle()'>
    <span class='dmx-arrow' id='dmx-arrow'>▼</span>
    <span class='dmx-title'>DMX Test</span>
    <span class='dmx-hint'>Pin 3 = DMX+ &nbsp;·&nbsp; Pin 5 = DMX- &nbsp;·&nbsp; 120 Ω terminator required</span>
  </div>
  <div class='dmx-body collapsed' id='dmx-body'>

    <!-- TX Section -->
    <div class='dmx-sect'>
      <div class='dmx-sect-title'>TX: Output (SPI → wireless → DMX out)</div>
      <div class='dmx-row'>
        <button class='dbtn primary' onclick='dmxInit()'>Init TX</button>
        <button class='dbtn black' onclick='dmxBlackout()'>Blackout</button>
        <button class='dbtn green' onclick='dmxFull()'>Full 100%</button>
        <button class='dbtn' id='fade-btn' onclick='dmxFadeToggle()'>Fade</button>
      </div>
      <div class='dmx-row'>
        <label>Ch</label>
        <input class='dmx-num' type='number' id='tx-s' value='1' min='1' max='512'>
        <label>to</label>
        <input class='dmx-num' type='number' id='tx-e' value='16' min='1' max='512'>
        <label>Val</label>
        <input class='dmx-slider' type='range' id='tx-v' min='0' max='255' value='0'
          oninput='dmxValSync(this.value)'>
        <input class='dmx-num' type='number' id='tx-vn' value='0' min='0' max='255' style='width:72px'
          oninput='dmxValSync(this.value)'>
        <span class='dmx-pct' id='tx-pct'>0 %</span>
        <button class='dbtn primary' onclick='dmxSetRange()'>Send</button>
      </div>
      <div class='dmx-row'>
        <button class='dbtn' onclick='dmxTxPageStep(-1)' title='Previous 64 channels'>◀</button>
        <span class='dmx-pg' id='tx-page-label'>Ch 1 to 64</span>
        <button class='dbtn' onclick='dmxTxPageStep(1)' title='Next 64 channels'>▶</button>
      </div>
      <div class='dmx-grid' id='dmx-tx-grid'></div>
    </div>

    <!-- RX Section -->
    <div class='dmx-sect'>
      <div class='dmx-sect-title'>RX: Monitor (DMX in → wireless → SPI)</div>
      <div class='dmx-row'>
        <button class='dbtn primary' onclick='dmxRead()'>Read</button>
        <button class='dbtn' id='auto-btn' onclick='dmxAutoToggle()'>Auto</button>
        <input class='dmx-num' type='number' id='rx-ms' value='500' min='100' max='9999' style='width:56px'>
        <label>ms</label>
      </div>
      <div class='dmx-row'>
        <label>Start ch</label>
        <input class='dmx-num' type='number' id='rx-s' value='1' min='1' max='512'>
        <label>Count</label>
        <input class='dmx-num' type='number' id='rx-n' value='64' min='1' max='512'>
      </div>
      <div class='dmx-grid' id='dmx-rx-grid'></div>
    </div>

  </div>
</div>

<div id='main'></div>
</div>

<div id='logbar'>
  <span id='logmsg'>Ready.</span>
  <span class='log-meta'>timotwo-debug.local · v1.0.0</span>
</div>

<script>
// ── Register definitions ───────────────────────────────────────
// t: access (r, w, rw)  sc: mode in which the register exists (rx, tx)
// d: description, one entry per line, no full stop at the end of a line
// rs: text of the confirmation shown before a write that can restart the module
const RS_MODE = 'Switching between RX and TX mode reboots the module';
const RS_RF   = 'The Dev Doc register map lists \x22Write may cause reboot\x22 for RF_PROTOCOL';
const RS_KEY  = 'Writing LINKING_KEY restarts the module\nPer Dev Doc, all non-volatile registers revert to their respective reset values';
const RS_BLE  = 'Writing BLE_ENABLE restarts the module\nPer Dev Doc, \x22all non-volatile registers will reset to their default values\x22';
const RS_PIN  = 'After writing BLE_PIN the module restarts\nPer Dev Doc, \x22all non-volatile registers will reset to their default values\x22';

const CATS = [
  { name: 'Configuration', regs: [
    { a:0x00, n:'CONFIG', l:1, t:'rw', rs:RS_MODE,
      d:['Bit7 = RADIO_ENABLE', 'Bit3 = SPI_RDM (RX only)', 'Bit1 = RADIO_TX_RX_MODE (0 = RX, 1 = TX)', 'Bit0 = UART_EN', 'A mode change restarts the module, all registers revert to reset values'] },
    { a:0x01, n:'STATUS', l:1, t:'rw',
      d:['Bit7 = UPDATE_MODE (R)', 'Bit3 = DMX (R)', 'Bit2 = IDENTIFY (TX: write 1 start, 0 stop; RX: read-only)', 'Bit1 = RF_LINK (TX: write 1 starts linking; read-only in RX)', 'Bit0 = LINKED (1 = linked or pairing; write 1 = unlink)'] },
  ]},
  { name: 'Interrupts', regs: [
    { a:0x02, n:'IRQ_MASK',      l:1, t:'rw',
      d:['Bit6 = EXTENDED_IRQ_EN', 'Bit5 = IDENTIFY_IRQ_EN', 'Bit4 = ASC_IRQ_EN', 'Bit3 = RF_LINK_IRQ_EN', 'Bit2 = DMX_CHANGED_IRQ_EN', 'Bit1 = LOST_DMX_IRQ_EN', 'Bit0 = RX_DMX_IRQ_EN'] },
    { a:0x03, n:'IRQ_FLAGS',     l:1, t:'r',
      d:['Bit7 = SPI_DEVICE_BUSY', 'Bit6 = EXTENDED_IRQ', 'Bit5 = IDENTIFY_IRQ', 'Bit4 = ASC_IRQ', 'Bit3 = RF_LINK_IRQ', 'Bit2 = DMX_CHANGED_IRQ', 'Bit1 = LOST_DMX_IRQ', 'Bit0 = RX_DMX_IRQ'] },
    { a:0x0A, n:'EXT_IRQ_MASK',  l:4, t:'rw',
      d:['Byte 3 (bytes 0-2 and Bit7 reserved)', 'Bit6 = UNIV_META_CHANGED_EN', 'Bit5 = RDM_DISCOVERY_EN', 'Bit4 = RADIO_MUTE_EN', 'Bit3 = RADIO_DISCOVERY_EN', 'Bit2 = RXTX_CTS_EN', 'Bit1 = RXTX_DA_EN', 'Bit0 = RDM_IRQ_EN'] },
    { a:0x0B, n:'EXT_IRQ_FLAGS', l:4, t:'rw',
      d:['Byte 3 (bytes 0-2 and Bit7 reserved)', 'Bit6 = UNIV_META_CHANGED', 'Bit5 = RDM_DISCOVERY', 'Bit4 = RADIO_MUTE', 'Bit3 = RADIO_DISCOVERY', 'Bit2 = RXTX_CTS', 'Bit1 = RXTX_DA', 'Bit0 = RDM_IRQ'] },
  ]},
  { name: 'DMX', regs: [
    { a:0x04, n:'DMX_WINDOW',  l:4, t:'rw',
      d:['Bytes 0-1 = window size (reset 512)', 'Bytes 2-3 = start address, 0 to 511 (zero-indexed)', 'The window must not span beyond slot 512'] },
    { a:0x05, n:'ASC_FRAME',   l:3, t:'r',
      d:['Bytes 0-1 = length of the received ASC frame', 'Byte 2 = start code'] },
    { a:0x08, n:'DMX_SPEC',    l:8, t:'rw', sc:'tx',
      d:['Bytes 0-1 = N_CHANNELS (1 to 512, reset 512)', 'Bytes 2-3 = interslot time in µs (reset 0)', 'Bytes 4-7 = refresh period in µs (reset 25000 = 40 Hz)'] },
    { a:0x09, n:'DMX_CONTROL', l:1, t:'rw', sc:'tx',
      d:['Bit0 = ENABLE (internal DMX generation from SPI data)', 'Set DMX_SPEC before enabling'] },
    { a:0x0D, n:'DMX_SOURCE',  l:1, t:'r',
      d:['Active data source', '0 = none', '1 = DMX port (UART)', '2 = wireless DMX link', '3 = SPI', '4 = BLE'] },
  ]},
  { name: 'RF / Radio', regs: [
    { a:0x06, n:'LINK_QUALITY',     l:1,  t:'r', sc:'rx',
      d:['PDR (packet delivery rate)', '0 = 0 %, 255 = 100 %'] },
    { a:0x0C, n:'RF_PROTOCOL',      l:1,  t:'rw', sc:'tx', rs:RS_RF,
      d:['TX protocol', '0 = CRMX', '1 = W-DMX G3', '3 = W-DMX G4S', 'Register map: write may cause reboot'] },
    { a:0x0E, n:'LOLLIPOP',         l:1,  t:'rw',
      d:['Lollipop counter', 'Starts at 0 after a restart', 'Increases by 1 on every read (including Read all)', '255 wraps to 128', 'After writing a value above 128, a read below 128 means the module restarted'] },
    { a:0x11, n:'RF_POWER',         l:1,  t:'rw', sc:'tx',
      d:['Output power, reset value 2', '2 = 20 dBm EIRP (2 dBi antenna), 18 dBm at the module', '3 = 16 dBm EIRP, 14 dBm at the module', '4 = 11 dBm EIRP, 9 dBm at the module', '5 = 5 dBm EIRP, 3 dBm at the module'] },
    { a:0x12, n:'BLOCKED_CHANNELS', l:11, t:'rw', sc:'tx',
      d:['11 bytes, Bit = 1 blocks a channel', 'Channels 2 to 80 = 2402 to 2480 MHz', 'Bits for channels 0, 1 and 81 to 87 are ignored', 'At least 16 channels must stay enabled, otherwise the register resets to all 0'] },
  ]},
  { name: 'Linking', regs: [
    { a:0x20, n:'BINDING_UID', l:6,  t:'rw',
      d:['RDM UID of the host device (fixture containing the module)', 'MSB first', 'Reset value 0'] },
    { a:0x21, n:'LINKING_KEY', l:10, t:'w', rs:RS_KEY,
      d:['Write only', 'Bytes 0-7 = key digits as values (12345678 = 01 02 03 04 05 06 07 08)', 'RX: 10 bytes, byte 8 = MODE (0 CRMX Classic, 1 CRMX2), byte 9 = OUTPUT (0 to 7)', 'TX: 8 bytes, eight FF restore the factory key'] },
  ]},
  { name: 'BLE / Device', regs: [
    { a:0x30, n:'BLE_STATUS',  l:1,  t:'rw', rs:RS_BLE,
      d:['Bit2 = BLE_IS_CONNECTED (R)', 'Bit1 = PIN_ACTIVE (R)', 'Bit0 = BLE_ENABLE (R/W)'] },
    { a:0x31, n:'BLE_PIN',     l:6,  t:'w', rs:RS_PIN,
      d:['Write only', '6 ASCII digits (30 to 39) enable the PIN', 'Six FF disable it'] },
    { a:0x32, n:'BATTERY',     l:1,  t:'w',
      d:['Write only', 'Battery level for BLE: 0 to 100 %', '255 = no battery (reset)', '101 to 254 reserved'] },
    { a:0x34, n:'OEM_INFO',    l:4,  t:'rw',
      d:['Bytes 0-1 = ESTA manufacturer ID', 'Bytes 2-3 = device model ID', 'Shown in the CRMX Toolbox connection list', 'The write response is delayed by 2 s'] },
    { a:0x35, n:'RXTX_STATUS', l:1,  t:'r',
      d:['Bit1 = CLEAR_TO_SEND', 'Bit0 = DATA_AVAILABLE', 'BLE RXTX service'] },
    { a:0x36, n:'DEVICE_NAME', l:32, t:'rw', str:true,
      d:['32 ASCII characters, NULL-terminated if shorter', 'Toolbox device name and RDM DEVICE_LABEL', 'TX mode: the first 16 characters are sent as universe name', 'The write response is delayed by 2 s'] },
  ]},
  { name: 'Universe', regs: [
    { a:0x33, n:'UNIVERSE_COLOR', l:3,  t:'rw',
      d:['RGB, 3 bytes', 'TX: R/W, sent to the receivers', 'RX: read-only, color received from the transmitter'] },
    { a:0x37, n:'UNIVERSE_NAME',  l:16, t:'r', sc:'rx',
      d:['16 ASCII characters, NULL-terminated if shorter', 'Universe name received from the transmitter'] },
  ]},
  { name: 'Info', regs: [
    { a:0x10, n:'VERSION',           l:8,  t:'r',
      d:['Bytes 0-3 = hardware version (e.g. 0000A003)', 'Bytes 4-7 = software version (e.g. 1.0.7.2)'] },
    { a:0x3F, n:'PRODUCT_ID',        l:4,  t:'r',
      d:['TimoTwo = F1 40 00 00'] },
    { a:0x3D, n:'INSTALLED_OPTIONS', l:13, t:'r',
      d:['Byte 0 = number of options (0 to 6)', 'Then 2 bytes per option', '0x2001 = RDM TX via SPI', '0x2002 = RDM TX via proxy'] },
  ]},
];

// ── State ──────────────────────────────────────────────────────
let curMode = null;   // 'rx' or 'tx', known after Read all

// ── Utilities ──────────────────────────────────────────────────
// All requests go through one queue: the web server of the ESP32 handles one connection at a time,
// parallel requests only pile up behind each other.
let apiChain = Promise.resolve();
function apiFetch(url, opts) {
  const job = () => fetch(url, opts);
  const p = apiChain.then(job, job);
  apiChain = p.then(() => {}, () => {});
  return p;
}
const hx = v => v.toString(16).toUpperCase().padStart(2,'0');
const rk = a => 'r'+a;
const esc = s => String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/\x22/g,'&quot;').replace(/'/g,'&#39;');
const SC_TEXT = { rx:'RX only', tx:'TX only' };

function setStatus(state, txt) {
  document.getElementById('stxt').textContent = txt;
  const d = document.getElementById('dot');
  d.className = 'dot'+(state==='ok'?' ok':state==='err'?' err':state==='busy'?' busy':'');
}
function log(msg, state='') {
  const el = document.getElementById('logmsg');
  el.textContent = msg;
  el.className = state;
}

// ── Decoded value interpreter ──────────────────────────────────
function names(b0, defs) {
  return defs.filter(d => b0 & d[0]).map(d => d[1]);
}

function decodeReg(addr, bytes) {
  if (!bytes || !bytes.length) return '';
  const b = bytes, b0 = b[0];
  try {
    switch (addr) {
      case 0x00: { // CONFIG
        const p = [];
        p.push((b0 & 0x80) ? 'RADIO ON' : 'RADIO OFF');
        p.push((b0 & 0x02) ? 'TX mode' : 'RX mode');
        if (b0 & 0x08) p.push('SPI-RDM');
        if (b0 & 0x01) p.push('UART');
        return p.join(' · ');
      }
      case 0x01: { // STATUS
        const p = names(b0, [[0x01,'LINKED'],[0x02,'RF_LINK'],[0x04,'IDENTIFY'],[0x08,'DMX'],[0x80,'UPDATE_MODE']]);
        return p.length ? p.join(' · ') : 'idle';
      }
      case 0x02: { // IRQ_MASK
        const p = names(b0, [[0x40,'EXTENDED_IRQ_EN'],[0x20,'IDENTIFY_IRQ_EN'],[0x10,'ASC_IRQ_EN'],[0x08,'RF_LINK_IRQ_EN'],
                             [0x04,'DMX_CHANGED_IRQ_EN'],[0x02,'LOST_DMX_IRQ_EN'],[0x01,'RX_DMX_IRQ_EN']]);
        return p.length ? p.join(' | ') : 'none';
      }
      case 0x03: { // IRQ_FLAGS
        const p = names(b0, [[0x80,'SPI_DEVICE_BUSY'],[0x40,'EXTENDED_IRQ'],[0x20,'IDENTIFY_IRQ'],[0x10,'ASC_IRQ'],
                             [0x08,'RF_LINK_IRQ'],[0x04,'DMX_CHANGED_IRQ'],[0x02,'LOST_DMX_IRQ'],[0x01,'RX_DMX_IRQ']]);
        return p.length ? p.join(' | ') : 'none';
      }
      case 0x0A: case 0x0B: { // EXTENDED_IRQ_MASK / EXTENDED_IRQ_FLAGS (byte 3)
        if (b.length < 4) return '';
        const sfx = addr === 0x0A ? '_EN' : '';
        const p = names(b[3], [[0x40,'UNIV_META_CHANGED'],[0x20,'RDM_DISCOVERY'],[0x10,'RADIO_MUTE'],[0x08,'RADIO_DISCOVERY'],
                               [0x04,'RXTX_CTS'],[0x02,'RXTX_DA'],[0x01,'RDM_IRQ']]).map(n => n + sfx);
        return p.length ? p.join(' | ') : 'none';
      }
      case 0x04: { // DMX_WINDOW
        if (b.length < 4) return '';
        const size = (b[0]<<8)|b[1], start = (b[2]<<8)|b[3];
        return `size: ${size}  start ch: ${start+1}`;
      }
      case 0x05: { // ASC_FRAME
        if (b.length < 3) return '';
        return `${(b[0]<<8)|b[1]} bytes  start code: 0x${hx(b[2])}`;
      }
      case 0x06: // LINK_QUALITY
        return `${Math.round(b0/2.55)}%   (${b0} / 255)`;
      case 0x08: { // DMX_SPEC
        if (b.length < 8) return '';
        const nCh = (b[0]<<8)|b[1];
        const ref = ((b[4]<<24)|(b[5]<<16)|(b[6]<<8)|b[7]) >>> 0;
        const hz  = ref > 0 ? Math.round(1000000/ref) : 0;
        return `${nCh} channels · ${ref} µs refresh (${hz} Hz) · ${(b[2]<<8)|b[3]} µs interslot`;
      }
      case 0x09: // DMX_CONTROL
        return (b0 & 0x01) ? 'DMX generation ENABLED' : 'DMX generation DISABLED';
      case 0x0C: // RF_PROTOCOL
        return ({0:'CRMX',1:'W-DMX G3',3:'W-DMX G4S'})[b0] || `0x${hx(b0)}`;
      case 0x0D: // DMX_SOURCE
        return (['none','UART','wireless','SPI','BLE'][b0] || `0x${hx(b0)}`) + ' source';
      case 0x0E: // LOLLIPOP
        return `counter: ${b0}`;
      case 0x10: { // VERSION
        if (b.length < 8) return '';
        return `HW: ${hx(b[0])}${hx(b[1])}${hx(b[2])}${hx(b[3])}   FW: ${b[4]}.${b[5]}.${b[6]}.${b[7]}`;
      }
      case 0x11: { // RF_POWER
        const eirp = {2:20,3:16,4:11,5:5}, mod = {2:18,3:14,4:9,5:3}, mw = {2:100,3:40,4:13,5:3};
        if (!eirp[b0]) return `0x${hx(b0)}`;
        return `${b0}: ${eirp[b0]} dBm / ${mw[b0]} mW EIRP (2 dBi antenna) · ${mod[b0]} dBm at the module`;
      }
      case 0x12: { // BLOCKED_CHANNELS
        const blocked = [];
        for (let ch = 2; ch <= 80; ch++) if (b[ch >> 3] & (1 << (ch & 7))) blocked.push(ch);
        return blocked.length ? `blocked: ${blocked.join(', ')}` : 'no channel blocked';
      }
      case 0x20: // BINDING_UID
        return 'UID: ' + Array.from(b).map(hx).join(':');
      case 0x30: // BLE_STATUS (bit values as read, polarity not interpreted)
        return `BLE_ENABLE=${b0 & 1} · PIN_ACTIVE=${(b0 >> 1) & 1} · BLE_IS_CONNECTED=${(b0 >> 2) & 1}`;
      case 0x33: // UNIVERSE_COLOR
        if (b.length < 3) return '';
        return `R: ${b[0]}  G: ${b[1]}  B: ${b[2]}`;
      case 0x34: { // OEM_INFO
        if (b.length < 4) return '';
        const esta = (b[0]<<8)|b[1], model = (b[2]<<8)|b[3];
        return `ESTA manufacturer ID: 0x${esta.toString(16).toUpperCase().padStart(4,'0')}   device model ID: 0x${model.toString(16).toUpperCase().padStart(4,'0')}`;
      }
      case 0x35: // RXTX_STATUS
        return ((b0&0x02)?'CTS  ':'') + ((b0&0x01)?'DATA AVAILABLE':'no data');
      case 0x36: case 0x37: { // DEVICE_NAME / UNIVERSE_NAME
        const s = Array.from(b).map(v=>(v>=0x20&&v<0x7f)?String.fromCharCode(v):'\0').join('').split('\0')[0];
        return s.length ? `\x22${s}\x22` : '(empty)';
      }
      case 0x3D: { // INSTALLED_OPTIONS
        const cnt = b[0];
        if (!cnt) return 'no options installed';
        const opts = [];
        for (let i=0; i<cnt && (1+i*2+1)<b.length; i++) {
          const code = (b[1+i*2]<<8)|b[2+i*2];
          opts.push(({0x2001:'RDM TX via SPI',0x2002:'RDM TX via proxy'})[code] || `0x${code.toString(16).toUpperCase()}`);
        }
        return opts.join(', ');
      }
      case 0x3F: // PRODUCT_ID
        return b.map(hx).join(' ') + ((b[0]===0xF1&&b[1]===0x40)?'   TimoTwo':'   unknown product');
      default:
        if (b.length === 1) return `${b0}  (0x${hx(b0)})`;
        return '';
    }
  } catch(e) { return ''; }
}

// ── Build empty placeholder cells ─────────────────────────────
function emptyVal(len) {
  let h='';
  const show = Math.min(len, 32);
  for (let i=0; i<show; i++) h+=`<span class='bx'>--</span>`;
  if (len>32) h+=`<span class='bx'>+${len-32}</span>`;
  return h;
}

function badge(t) {
  return `<span class='badge b-${t}'>${t.toUpperCase()}</span>`;
}

// Register length expected by a write (LINKING_KEY: 10 bytes in RX, 8 in TX)
function writeLen(reg) {
  if (reg.a === 0x21) return curMode === 'tx' ? 8 : curMode === 'rx' ? 10 : null;
  return reg.l;
}

// ── Build a single register row ────────────────────────────────
function buildRow(reg) {
  const k   = rk(reg.a);
  const hr  = reg.t==='r'||reg.t==='rw';
  const hw  = reg.t==='w'||reg.t==='rw';
  const plen = Math.min(reg.l, 8);
  const ph  = Array.from({length:plen},()=>'XX').join(' ');

  const readBtn = hr ? `<button class='rbtn' onclick='doRead(${reg.a},${reg.l})'>R</button>` : '';

  let writeCtrl = '';
  if (hw && reg.str) {
    // String-type register: plain text input, auto-converted to ASCII hex on write
    writeCtrl = `<input class='hex' id='inp-${k}' data-str='1' data-len='${reg.l}'
                   placeholder='ASCII text, max ${reg.l-1} chars' style='width:170px'>
                 <button class='wbtn' onclick='doWrite(${reg.a})'>W</button>`;
  } else if (hw) {
    writeCtrl = `<input class='hex' id='inp-${k}' placeholder='${ph}' style='width:${Math.max(72, plen*27)}px'>
                 <button class='wbtn' onclick='doWrite(${reg.a})'>W</button>`;
  }

  const tags = (reg.sc ? `<span class='tag'>${SC_TEXT[reg.sc]}</span>` : '')
             + (reg.rs ? `<span class='tag rst' title='${esc(reg.rs)}'>restart</span>` : '');

  return `<tr>
    <td class='c-addr'>0x${hx(reg.a)}</td>
    <td class='c-name'>${reg.n}${tags ? `<div class='tags'>${tags}</div>` : ''}</td>
    <td class='c-type'>${badge(reg.t)}</td>
    <td class='c-len'>${reg.l}B</td>
    <td class='c-desc'>${reg.d.map(esc).join('<br>')}</td>
    <td class='c-val'>
      <div class='vwrap' id='val-${k}' data-addr='${reg.a}'>${emptyVal(reg.l)}</div>
      <div class='vdec' id='dec-${k}'></div>
    </td>
    <td class='c-ctrl'>
      <div class='ctrl'>
        ${readBtn}${writeCtrl}
        <span class='msg' id='msg-${k}'></span>
      </div>
    </td>
  </tr>`;
}

// ── Render all categories ──────────────────────────────────────
function render() {
  let h = '';
  CATS.forEach((cat, ci) => {
    h += `<div class='cat'>
      <div class='cat-hd' onclick='toggle(${ci})'>
        <span class='cat-arrow' id='arr-${ci}'>▼</span>
        <span class='cat-name'>${cat.name}</span>
        <span class='cat-cnt'>${cat.regs.length} registers</span>
      </div>
      <div class='cat-body' id='cat-${ci}'>
        <div class='tbl-wrap'><table>
          <colgroup>
            <col class='w-addr'><col class='w-name'><col class='w-type'><col class='w-len'>
            <col class='w-desc'><col class='w-val'><col class='w-act'>
          </colgroup>
          <thead><tr>
            <th>Addr</th><th>Register</th><th>Type</th><th>Len</th>
            <th>Description</th><th>Value / Decoded</th><th>Action</th>
          </tr></thead>
          <tbody>${cat.regs.map(buildRow).join('')}</tbody>
        </table></div>
      </div>
    </div>`;
  });
  document.getElementById('main').innerHTML = h;
}

function toggle(ci) {
  const b = document.getElementById('cat-'+ci);
  const a = document.getElementById('arr-'+ci);
  const hidden = b.classList.toggle('hidden');
  a.classList.toggle('closed', hidden);
}

// ── Show register value + decoded text ────────────────────────
function showVal(bytes, key) {
  const el  = document.getElementById('val-'+key);
  const dec = document.getElementById('dec-'+key);
  if (!el) return;

  const addr = parseInt(el.dataset.addr);
  const show = Math.min(bytes.length, 32);

  let h = '';
  for (let i=0; i<show; i++) h += `<span class='bx live'>${hx(bytes[i])}</span>`;
  if (bytes.length > 32) h += `<span class='bx'>+${bytes.length-32}</span>`;
  el.innerHTML = h;

  setTimeout(() => el.querySelectorAll('.bx').forEach(b => b.classList.remove('live')), 900);

  if (dec && !isNaN(addr)) dec.textContent = decodeReg(addr, bytes);
}

// Register that does not exist in the current mode: neutral text instead of an error
function showSkipped(key, why) {
  const dec = document.getElementById('dec-'+key);
  if (dec) dec.textContent = why === 'tx_only' ? 'TX only, module is in RX mode' : 'RX only, module is in TX mode';
}

function setMsg(key, txt, cls) {
  const el = document.getElementById('msg-'+key);
  if (el) { el.textContent=txt; el.className='msg '+cls; }
}

function clearVals() {
  CATS.forEach(cat => cat.regs.forEach(reg => {
    const k = rk(reg.a);
    const v = document.getElementById('val-'+k);
    const d = document.getElementById('dec-'+k);
    if (v) v.innerHTML = emptyVal(reg.l);
    if (d) d.textContent = '';
    setMsg(k, '', '');
  }));
  curMode = null;
  setStatus('', 'Idle');
  log('Cleared.');
}

// ── API: read single register ──────────────────────────────────
async function doRead(addr, len) {
  const k = rk(addr);
  setMsg(k, '…', '');
  try {
    const r = await apiFetch(`/api/read?addr=${addr}&len=${len}`);
    const j = await r.json();
    if (j.ok) {
      showVal(j.bytes, k);
      setMsg(k, '✓', 'ok');
      log(`READ  0x${hx(addr)}  →  ${j.bytes.map(hx).join(' ')}`, 'ok');
    } else {
      setMsg(k, '✗', 'err');
      log(`Read error  0x${hx(addr)}: ${j.error||'?'}`, 'err');
    }
  } catch(e) {
    setMsg(k, '?', 'err');
    setStatus('err', 'Offline');
    log('Connection error', 'err');
  }
}

// ── Input handling for writes ──────────────────────────────────
// Hex input: optional 0x prefix, spaces allowed. Returns {hex} or {err}.
function parseHexInput(raw) {
  let s = String(raw).trim();
  if (/^0x/i.test(s)) s = s.slice(2);
  s = s.replace(/\s+/g, '');
  if (!s.length) return { err: 'Enter hex bytes, e.g. 82 or 08 B6 17 70' };
  const bad = s.match(/[^0-9a-fA-F]/);
  if (bad) return { err: `Invalid character \x22${bad[0]}\x22, only hex digits 0-9 and A-F are allowed` };
  if (s.length % 2) return { err: 'Odd number of hex digits, bytes need two digits each' };
  return { hex: s.toUpperCase() };
}

// Text input of a string register: ASCII 0x20 to 0x7E, null-padded to the register length
function parseTextInput(text, len) {
  if (text.length > len - 1) return { err: `Maximum ${len-1} characters` };
  const bytes = [];
  for (let i = 0; i < text.length; i++) {
    const c = text.charCodeAt(i);
    if (c < 0x20 || c > 0x7E) return { err: `Character \x22${text[i]}\x22 is not printable ASCII` };
    bytes.push(c);
  }
  while (bytes.length < len) bytes.push(0);
  return { hex: bytes.map(v => hx(v)).join('') };
}

function findReg(addr) {
  for (const cat of CATS) for (const reg of cat.regs) if (reg.a === addr) return reg;
  return null;
}

// ── API: write single register ─────────────────────────────────
async function doWrite(addr) {
  const key = rk(addr);
  const inp = document.getElementById('inp-'+key);
  if (!inp) return;
  const reg = findReg(addr);

  const p = inp.dataset.str === '1'
    ? parseTextInput(inp.value, parseInt(inp.dataset.len) || 32)
    : parseHexInput(inp.value);
  if (p.err) { setMsg(key, '✗', 'err'); log(`Write 0x${hx(addr)}: ${p.err}`, 'err'); return; }
  const hex = p.hex;

  const want = reg ? writeLen(reg) : null;
  if (want !== null && hex.length / 2 !== want) {
    setMsg(key, '✗', 'err');
    log(`Write 0x${hx(addr)}: ${want} bytes expected, ${hex.length/2} given`, 'err');
    return;
  }
  if (reg && reg.rs && !confirm(`${reg.n}\n\n${reg.rs}\n\nWrite anyway?`)) {
    log(`Write 0x${hx(addr)} cancelled`);
    return;
  }

  setMsg(key, '…', '');
  try {
    const r = await apiFetch('/api/write', { method: 'POST', body: new URLSearchParams({ addr: String(addr), data: hex }) });
    const j = await r.json();
    if (j.ok) {
      setMsg(key, '✓', 'ok');
      let note = '';
      if (j.restart) note = j.ready ? ' · module restarted, answering again' : ' · module restarted, not answering yet';
      if (j.ack_lost) note += ' · acknowledge lost, CONFIG readback confirms the new mode';
      log(`WRITE 0x${hx(addr)}  ←  ${hex.match(/.{2}/g).join(' ')}${note}`, 'ok');
    } else {
      setMsg(key, '✗', 'err');
      log(`Write error  0x${hx(addr)}: ${j.error||'?'}`, 'err');
    }
  } catch(e) {
    setMsg(key, '?', 'err');
    setStatus('err', 'Offline');
    log('Connection error', 'err');
  }
}

// ── API: read all readable registers ──────────────────────────
async function readAll() {
  setStatus('busy', 'Reading…');
  log('Reading all registers…');
  try {
    const r = await apiFetch('/api/readall');
    const j = await r.json();
    if (j.ok) {
      curMode = j.mode;
      let ok=0, fail=0, skip=0;
      Object.entries(j.registers).forEach(([aStr, v]) => {
        const key = rk(parseInt(aStr, 16));
        if (v.ok) { showVal(v.bytes, key); ok++; }
        else if (v.skipped) { showSkipped(key, v.skipped); skip++; }
        else {
          const el = document.getElementById('val-'+key);
          if (el) el.querySelectorAll('.bx').forEach(b=>{b.classList.add('err');b.textContent='??';});
          fail++;
        }
      });
      setStatus(ok > 0 ? 'ok' : 'err', ok > 0 ? `OK · ${j.mode.toUpperCase()} mode` : 'SPI Error');
      log(`${ok} registers read${skip ? ' · ' + skip + ' not available in ' + j.mode.toUpperCase() + ' mode' : ''}${fail ? ' · ' + fail + ' unreadable (??)' : ''}.`, ok > 0 ? 'ok' : 'err');
    } else {
      setStatus('err', 'Error');
      log('Read all failed: ' + (j.error || '?'), 'err');
    }
  } catch(e) {
    setStatus('err', 'Offline');
    log('Connection error', 'err');
  }
}

// ── DMX panel ──────────────────────────────────────────────────
const dmxTxBuf = new Uint8Array(512);
let dmxAutoTimer = null, dmxFadeTimer = null, dmxFadeDir = 1, dmxFadeVal = 0;
let dmxJob = null, dmxPending = false, dmxLastMs = 0, dmxReading = false;

function dmxToggle() {
  const b = document.getElementById('dmx-body');
  const a = document.getElementById('dmx-arrow');
  b.classList.toggle('collapsed');
  a.classList.toggle('closed', b.classList.contains('collapsed'));
}

// TX grid: 64 cells per page, 8 pages cover channels 1 to 512. Cells are clickable and
// set that channel to the current slider value.
const DMX_PAGE = 64;
let dmxTxPage = 0;

function dmxTxBuild() {
  const g = document.getElementById('dmx-tx-grid');
  if (!g) return;
  const base = dmxTxPage * DMX_PAGE;
  let html = '';
  for (let i = 0; i < DMX_PAGE; i++) {
    html += `<div class='dmx-cell clickable' onclick='dmxTxClick(${i})' title='Ch ${base+i+1}: click to set'>
      <span class='dmx-ch'>${String(base+i+1).padStart(3,'0')}</span>
      <div class='dmx-bar-wrap'><div class='dmx-bar' id='tbar-${i}' style='width:0%'></div></div>
      <span class='dmx-val' id='tval-${i}'>0</span></div>`;
  }
  g.innerHTML = html;
  document.getElementById('tx-page-label').textContent = `Ch ${base+1} to ${base+DMX_PAGE}`;
  dmxTxRefresh();
}

function dmxTxPageStep(d) {
  const last = 512 / DMX_PAGE - 1;
  dmxTxPage = Math.max(0, Math.min(last, dmxTxPage + d));
  dmxTxBuild();
}

function dmxTxRefresh() {
  const base = dmxTxPage * DMX_PAGE;
  for (let i = 0; i < DMX_PAGE; i++) {
    const v = dmxTxBuf[base + i];
    const b = document.getElementById('tbar-'+i);
    const t = document.getElementById('tval-'+i);
    if (b) b.style.width = Math.round(v/255*100)+'%';
    if (t) t.textContent = v;
  }
}

// Build RX grid with n cells starting at ch start+1
function dmxRxBuild(start, n) {
  const g = document.getElementById('dmx-rx-grid');
  if (!g) return;
  let html = '';
  for (let i = 0; i < n; i++) {
    html += `<div class='dmx-cell' title='Ch ${start+i+1}'>
      <span class='dmx-ch'>${String(start+i+1).padStart(3,'0')}</span>
      <div class='dmx-bar-wrap'><div class='dmx-bar' id='rbar-${i}' style='width:0%'></div></div>
      <span class='dmx-val' id='rval-${i}'>--</span></div>`;
  }
  g.innerHTML = html;
}

// Init TX: switches to TX mode if the module is in RX mode (it reboots), writes 512 zeros
// (all slots written once), then enables the internal DMX generation (DMX_CONTROL bit 0)
async function dmxInit() {
  dmxFadeStop();
  try {
    const m = await (await apiFetch('/api/read?addr=0&len=1')).json();
    if (!m.ok) { log('CONFIG read: ' + (m.error||'error'), 'err'); return; }
    const cfg = m.bytes[0];
    if (!(cfg & 0x02)) {
      if (!confirm(RS_MODE + '\n\nSwitch to TX and initialize?')) { log('Init TX cancelled'); return; }
      log('Switching to TX mode, the module reboots…');
      const w = await (await apiFetch('/api/write', { method: 'POST', body: new URLSearchParams({ addr: '0', data: hx(cfg | 0x02) }) })).json();
      if (!w.ok) { log('Switch to TX failed: ' + (w.error||'error'), 'err'); return; }
      if (w.ready === false) { log('The module does not answer after the reboot', 'err'); return; }
      curMode = 'tx';
    }
  } catch(e) { setStatus('err','Offline'); log('Connection error','err'); return; }
  dmxTxBuf.fill(0);
  dmxTxRefresh();
  const ok = await dmxSend();
  if (!ok) return;
  try {
    const r = await apiFetch('/api/write', { method: 'POST', body: new URLSearchParams({ addr: '9', data: '01' }) });  // DMX_CONTROL bit0 = enable
    const j = await r.json();
    if (j.ok) log('TX initialized: 512 channels written, DMX generation enabled', 'ok');
    else log('DMX_CONTROL write: ' + (j.error||'error'), 'err');
  } catch(e) { setStatus('err','Offline'); log('Connection error','err'); }
}

async function dmxBlackout() {
  dmxFadeStop();
  dmxTxBuf.fill(0);
  dmxTxRefresh();
  if (await dmxSend()) log(`Blackout: all 512 channels = 0 (${dmxLastMs} ms)`, 'ok');
}

async function dmxFull() {
  dmxFadeStop();
  dmxTxBuf.fill(255);
  dmxTxRefresh();
  if (await dmxSend()) log(`Full 100%: all 512 channels = 255 (${dmxLastMs} ms)`, 'ok');
}

// Channel range of the 'Ch ... to ...' fields as zero-indexed [first, last], clamped to 0..511
function dmxRange() {
  let a = parseInt(document.getElementById('tx-s').value);
  let b = parseInt(document.getElementById('tx-e').value);
  if (isNaN(a)) a = 1;
  if (isNaN(b)) b = a;
  a = Math.max(1, Math.min(512, a)) - 1;
  b = Math.max(1, Math.min(512, b)) - 1;
  return [Math.min(a, b), Math.max(a, b)];
}

async function dmxSetRange() {
  dmxFadeStop();
  const [s, e] = dmxRange();
  const v = Math.max(0, Math.min(255, parseInt(document.getElementById('tx-vn').value) || 0));
  for (let i = s; i <= e; i++) dmxTxBuf[i] = v;
  dmxTxRefresh();
  if (await dmxSend()) log(`Ch ${s+1} to ${e+1} = ${v} (${dmxLastMs} ms)`, 'ok');
}

// Stops a running fade. Manual actions call this first, otherwise the fade overwrites their values.
function dmxFadeStop() {
  if (!dmxFadeTimer) return;
  const btn = document.getElementById('fade-btn');
  clearInterval(dmxFadeTimer); dmxFadeTimer = null;
  btn.textContent = 'Fade'; btn.classList.remove('on');
}

function dmxFadeToggle() {
  const btn = document.getElementById('fade-btn');
  if (dmxFadeTimer) {
    dmxFadeStop();
  } else {
    btn.textContent = 'Stop fade'; btn.classList.add('on');
    dmxFadeTimer = setInterval(async () => {
      if (dmxJob) return;                           // skip a step while a transfer is still in flight
      dmxFadeVal += dmxFadeDir * 4;
      if (dmxFadeVal >= 255) { dmxFadeVal = 255; dmxFadeDir = -1; }
      if (dmxFadeVal <= 0)   { dmxFadeVal = 0;   dmxFadeDir =  1; }
      const [s, e] = dmxRange();                    // fades the channels of the 'Ch ... to ...' fields
      for (let i = s; i <= e; i++) dmxTxBuf[i] = dmxFadeVal;
      dmxTxRefresh();
      await dmxSend();
    }, 40);  // up to ~25 Hz fade
  }
}

// Slider and number field show the same value, with the percentage next to them
function dmxValSync(raw) {
  let v = parseInt(raw); if (isNaN(v)) v = 0;
  v = Math.max(0, Math.min(255, v));
  document.getElementById('tx-v').value = v;
  const n = document.getElementById('tx-vn');
  if (document.activeElement !== n) n.value = v;
  document.getElementById('tx-pct').textContent = Math.round(v / 255 * 100) + ' %';
}

// Click a TX cell (index within the visible page): set that channel to current slider value
function dmxTxClick(i) {
  dmxFadeStop();
  const ch = dmxTxPage * DMX_PAGE + i;
  const v = Math.max(0, Math.min(255, parseInt(document.getElementById('tx-vn').value) || 0));
  dmxTxBuf[ch] = v;
  dmxTxRefresh();
  dmxSend().then(ok => { if (ok) log(`Ch ${ch+1} = ${v} (${dmxLastMs} ms)`, 'ok'); });
}

// One POST of the full TX buffer to /api/dmx/write (one 512-byte SPI block), duration in dmxLastMs
async function dmxPost() {
  const t0 = Date.now();
  try {
    let hex = '';
    dmxTxBuf.forEach(v => hex += v.toString(16).padStart(2,'0').toUpperCase());
    const body = new URLSearchParams({data: hex});
    const r = await apiFetch('/api/dmx/write', {method:'POST', body});
    const j = await r.json();
    dmxLastMs = Date.now() - t0;
    if (!j.ok) log('DMX write: ' + (j.error||'error'), 'err');
    return j.ok;
  } catch(e) { setStatus('err','Offline'); log('Connection error','err'); return false; }
}

// Only one DMX transfer is in flight. Sends that arrive meanwhile are merged: the newest buffer follows once.
function dmxSend() {
  if (dmxJob) { dmxPending = true; return dmxJob; }
  dmxJob = (async () => {
    let ok = true;
    try { do { dmxPending = false; ok = await dmxPost(); } while (dmxPending && ok); }
    finally { dmxJob = null; }
    return ok;
  })();
  return dmxJob;
}

// Read DMX window from TimoTwo (one READ_DMX block)
async function dmxRead() {
  if (dmxReading) return;                           // Auto does not stack reads
  dmxReading = true;
  const s = Math.max(0, Math.min(511, (parseInt(document.getElementById('rx-s').value) || 1) - 1));
  const n = Math.max(1, Math.min(512 - s, parseInt(document.getElementById('rx-n').value) || 64));
  try {
    const r = await apiFetch(`/api/dmx/read?start=${s}&count=${n}`);
    const j = await r.json();
    if (j.ok) {
      const g = document.getElementById('dmx-rx-grid');
      if (!g) return;
      if (g.children.length !== j.count) dmxRxBuild(j.start, j.count);
      j.data.forEach((v, i) => {
        const b = document.getElementById('rbar-'+i);
        const t = document.getElementById('rval-'+i);
        if (b) b.style.width = Math.round(v/255*100)+'%';
        if (t) t.textContent = v;
      });
      log(`RX: ${j.count} ch read (ch ${j.start+1} to ${j.start+j.count})`, 'ok');
    } else log('DMX RX: ' + (j.error||'error'), 'err');
  } catch(e) { setStatus('err','Offline'); log('Connection error','err'); }
  finally { dmxReading = false; }
}

function dmxAutoToggle() {
  const btn = document.getElementById('auto-btn');
  if (dmxAutoTimer) {
    clearInterval(dmxAutoTimer); dmxAutoTimer = null;
    btn.textContent = 'Auto'; btn.classList.remove('on');
  } else {
    const ms = Math.max(100, parseInt(document.getElementById('rx-ms').value) || 500);
    dmxAutoTimer = setInterval(dmxRead, ms);
    btn.textContent = 'Stop auto'; btn.classList.add('on');
    dmxRead();
  }
}

// Init
render();
dmxTxBuild();
dmxRxBuild(0, 64);
</script>
)rawliteral";

// ════════════════════════════════════════════════════════════════
//  setup() and loop()
// ════════════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println("\n=== TimoTwo ESP32 Web SPI Debugger | LUMEN CONTROL ===");
    Serial.println("Version " SKETCH_VERSION);

    // Configure GPIO pins
    pinMode(PIN_CS,   OUTPUT);  digitalWrite(PIN_CS, HIGH);
    pinMode(PIN_IRQ,  INPUT_PULLUP);
    pinMode(PIN_LINK, INPUT_PULLUP);

    // Start VSPI bus
    timoSPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
    delay(300);
    Serial.println("[SPI] ready");

    // Connect to the WiFi network from secrets.h
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);                       // no modem sleep: lower latency of the web server
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_CONNECT_TIMEOUT_MS) {
        delay(300);
        Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());
        if (MDNS.begin(MDNS_NAME)) {
            MDNS.addService("http", "tcp", 80);
            Serial.printf("[mDNS] Reachable at http://%s.local\n", MDNS_NAME);
        } else {
            Serial.println("[mDNS] Failed to start, use the IP address above instead");
        }
    } else {
        Serial.println("[WiFi] FAILED to connect, check SSID/password in secrets.h or signal range.");
    }

    // Register HTTP routes
    server.on("/",             HTTP_GET,  []{ server.send_P(200, "text/html", INDEX_HTML); });
    server.on("/api/read",     HTTP_GET,  handleRead);
    server.on("/api/write",    HTTP_POST, handleWrite);
    server.on("/api/readall",  HTTP_GET,  handleReadAll);
    server.on("/api/dmx/write",HTTP_POST, handleDmxWrite);
    server.on("/api/dmx/read", HTTP_GET,  handleDmxRead);
    server.onNotFound([]{ server.send(404, "text/plain", "Not found"); });

    server.begin();
    Serial.println("[HTTP] Server running, see address above (mDNS or IP)");
}

void loop() {
    const uint32_t t0 = millis();
    server.handleClient();
    const uint32_t d = millis() - t0;
    if (d > 300) Serial.printf("[HTTP] request handling took %lu ms\n", (unsigned long)d);
}

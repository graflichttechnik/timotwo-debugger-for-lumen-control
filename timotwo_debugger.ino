/**
 * ╔════════════════════════════════════════════════════════════════╗
 *  TimoTwo SPI Register Debugger
 *  Radical Wireless LUMEN CONTROL | ESP32-WROOM-32 & Co. dev boards
 * ╠════════════════════════════════════════════════════════════════╣
 *  WiFi Access Point: "TimoTwo-Debug"  (no password)
 *  Web UI:            http://192.168.4.1
 * ╠════════════════════════════════════════════════════════════════╣
 *  Author: Stefan Graf - stefan@graf-lichttechnik.de
 *  Version: 1.0 - April 2026
 * ╠════════════════════════════════════════════════════════════════╣
 *  Wiring (LUMEN CONTROL 10-pin header):
 *    Pin  1  VDD  → 5V (buck conversion to 3.3V)
 *    Pin  2  CS   → GPIO  5
 *    Pin  3  DMX+ → nc (optional)
 *    Pin  4  SCK  → GPIO 18
 *    Pin  5  DMX- → nc (optional)
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
#include <WebServer.h>

// ── Pins (ESP32-WROOM-32, VSPI) ───────────────────────────────
#define PIN_CS     5   // LUMEN header pin 2
#define PIN_MOSI  23   // LUMEN header pin 6
#define PIN_MISO  19   // LUMEN header pin 8
#define PIN_SCK   18   // LUMEN header pin 4
#define PIN_IRQ    4   // LUMEN header pin 7
#define PIN_LINK   2   // LUMEN header pin 9 (optional)

// ── WiFi access point ─────────────────────────────────────────
const char* AP_SSID = "TimoTwo-Debug";

// ── SPI ───────────────────────────────────────────────────────
SPIClass timoSPI(VSPI);
#define SPI_FREQ        1000000UL   // 1 MHz (max allowed: 2 MHz)
#define IRQ_TIMEOUT_MS  200

WebServer server(80);

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

// Read `len` bytes from register at `addr`
bool spiReadReg(uint8_t addr, uint8_t* buf, uint8_t len) {
    if (!buf || len == 0 || len > 64) return false;
    uint8_t cmd = addr & 0x3F;   // READ_REG: 00AA AAAA

    // Phase 1: command byte — retry loop on SPI_DEVICE_BUSY (non-recursive)
    uint8_t flags = 0;
    for (uint8_t attempt = 0; attempt < 8; attempt++) {
        timoSPI.beginTransaction(SPISettings(SPI_FREQ, MSBFIRST, SPI_MODE0));
        digitalWrite(PIN_CS, LOW);
        delayMicroseconds(4);
        flags = timoSPI.transfer(cmd);
        digitalWrite(PIN_CS, HIGH);
        timoSPI.endTransaction();
        if (!(flags & 0x80)) break;              // not busy
        if (!waitIRQ(true)) return false;
    }
    if (flags & 0x80) return false;

    if (!waitIRQ(false)) return false;           // IRQ LOW: command acknowledged

    // Phase 2: read payload (first byte discarded by TimoTwo)
    timoSPI.beginTransaction(SPISettings(SPI_FREQ, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_CS, LOW);
    delayMicroseconds(4);
    timoSPI.transfer(0xFF);                      // dummy byte — required by protocol
    for (uint8_t i = 0; i < len; i++) buf[i] = timoSPI.transfer(0x00);
    digitalWrite(PIN_CS, HIGH);
    timoSPI.endTransaction();

    return waitIRQ(true);                        // IRQ HIGH: transfer complete
}

// Write `len` bytes to register at `addr`
bool spiWriteReg(uint8_t addr, const uint8_t* buf, uint8_t len) {
    if (!buf || len == 0 || len > 64) return false;
    uint8_t cmd = 0x40 | (addr & 0x3F);         // WRITE_REG: 01AA AAAA

    // Phase 1: command byte — retry loop on SPI_DEVICE_BUSY (non-recursive)
    uint8_t flags = 0;
    for (uint8_t attempt = 0; attempt < 8; attempt++) {
        timoSPI.beginTransaction(SPISettings(SPI_FREQ, MSBFIRST, SPI_MODE0));
        digitalWrite(PIN_CS, LOW);
        delayMicroseconds(4);
        flags = timoSPI.transfer(cmd);
        digitalWrite(PIN_CS, HIGH);
        timoSPI.endTransaction();
        if (!(flags & 0x80)) break;
        Serial.printf("[SPI] WRITE 0x%02X busy, attempt %d\n", addr, attempt + 1);
        if (!waitIRQ(true)) { Serial.printf("[SPI] WRITE 0x%02X: IRQ timeout after busy\n", addr); return false; }
    }
    if (flags & 0x80) { Serial.printf("[SPI] WRITE 0x%02X: device stayed busy\n", addr); return false; }

    if (!waitIRQ(false)) {
        Serial.printf("[SPI] WRITE 0x%02X: IRQ timeout (command phase)\n", addr);
        return false;
    }

    // Phase 2: write payload
    timoSPI.beginTransaction(SPISettings(SPI_FREQ, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_CS, LOW);
    delayMicroseconds(4);
    timoSPI.transfer(0xFF);                      // dummy byte — required by protocol
    for (uint8_t i = 0; i < len; i++) timoSPI.transfer(buf[i]);
    digitalWrite(PIN_CS, HIGH);
    timoSPI.endTransaction();

    if (!waitIRQ(true)) {
        Serial.printf("[SPI] WRITE 0x%02X: IRQ timeout (payload phase)\n", addr);
        return false;
    }

    Serial.printf("[SPI] WRITE 0x%02X OK (%d B):", addr, len);
    for (uint8_t i = 0; i < len; i++) Serial.printf(" %02X", buf[i]);
    Serial.println();
    return true;
}

// ════════════════════════════════════════════════════════════════
//  HTTP handler: GET /api/read?addr=XX&len=N
// ════════════════════════════════════════════════════════════════

void handleRead() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (!server.hasArg("addr") || !server.hasArg("len")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"addr and len required\"}");
        return;
    }
    uint8_t addr = (uint8_t)strtol(server.arg("addr").c_str(), nullptr, 0);
    uint8_t len  = (uint8_t)constrain(atoi(server.arg("len").c_str()), 1, 64);
    uint8_t buf[64] = {};
    bool ok = spiReadReg(addr, buf, len);
    String j = ok ? "{\"ok\":true,\"bytes\":[" : "{\"ok\":false,\"bytes\":[";
    for (uint8_t i = 0; i < len; i++) { j += buf[i]; if (i < len - 1) j += ','; }
    j += "]}";
    server.send(200, "application/json", j);
}

// ════════════════════════════════════════════════════════════════
//  HTTP handler: GET /api/write?addr=XX&data=AABBCC
// ════════════════════════════════════════════════════════════════

void handleWrite() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (!server.hasArg("addr") || !server.hasArg("data")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"addr and data required\"}");
        return;
    }
    uint8_t addr = (uint8_t)strtol(server.arg("addr").c_str(), nullptr, 0);
    String  hex  = server.arg("data");
    hex.trim();
    // String::replace() only removes first occurrence — loop to remove all spaces
    while (hex.indexOf(' ') >= 0) hex.replace(" ", "");
    hex.toUpperCase();

    if (hex.length() == 0 || hex.length() % 2 != 0) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"data must be an even number of hex digits\"}");
        return;
    }
    uint8_t len = (uint8_t)min((int)(hex.length() / 2), 64);
    uint8_t buf[64] = {};
    for (uint8_t i = 0; i < len; i++)
        buf[i] = (uint8_t)strtol(hex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);

    bool ok = spiWriteReg(addr, buf, len);
    server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"SPI timeout\"}");
}

// ════════════════════════════════════════════════════════════════
//  HTTP handler: GET /api/readall
// ════════════════════════════════════════════════════════════════

void handleReadAll() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    struct RegInfo { uint8_t addr, len; };
    static const RegInfo readable[] = {
        {0x00,1},{0x01,1},{0x02,1},{0x03,1},
        {0x04,4},{0x05,3},{0x06,1},
        {0x08,8},{0x09,1},{0x0A,4},{0x0B,4},
        {0x0C,1},{0x0D,1},{0x0E,1},
        {0x10,8},{0x11,1},{0x12,11},
        {0x20,6},{0x30,1},{0x33,3},{0x34,4},
        {0x35,1},{0x36,32},{0x37,16},
        {0x3D,13},{0x3F,4}
    };
    const int n = sizeof(readable) / sizeof(readable[0]);
    String j = "{\"ok\":true,\"registers\":{";
    for (int i = 0; i < n; i++) {
        uint8_t buf[64] = {};
        bool ok = spiReadReg(readable[i].addr, buf, readable[i].len);
        j += '"'; j += readable[i].addr; j += "\":{\"ok\":";
        j += ok ? "true" : "false";
        j += ",\"bytes\":[";
        for (uint8_t b = 0; b < readable[i].len; b++) { j += buf[b]; if (b < readable[i].len - 1) j += ','; }
        j += "]}";
        if (i < n - 1) j += ',';
        yield();
    }
    j += "}}";
    server.send(200, "application/json", j);
}

// ════════════════════════════════════════════════════════════════
//  Web UI
// ════════════════════════════════════════════════════════════════

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, minimum-scale=1.0">
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
#main { padding: 14px 16px 0; display: flex; flex-direction: column; gap: 12px; }

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
table { width: 100%; border-collapse: collapse; }
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
.c-addr { font-family: var(--mono); color: var(--secondary); font-size: var(--fs-sm); white-space: nowrap; width: 66px; padding-top: 12px !important; }
.c-name { font-family: var(--mono); color: var(--text); font-weight: 700; font-size: var(--fs-base); width: 185px; white-space: nowrap; padding-top: 12px !important; }
.c-type { width: 44px; text-align: center; padding-top: 12px !important; }
.c-len  { color: var(--secondary); font-size: var(--fs-sm); text-align: center; width: 40px; padding-top: 12px !important; }
.c-desc { color: var(--secondary); font-size: var(--fs-sm); line-height: 1.55; padding-top: 11px !important; }
.c-val  { min-width: 160px; max-width: 260px; }
.c-ctrl { white-space: nowrap; padding-top: 9px !important; }

/* ── Type badges ── */
.badge {
  display: inline-block; padding: 3px 7px; border-radius: 5px;
  font-family: var(--mono); font-size: var(--fs-xs); font-weight: 700;
  letter-spacing: .04em; min-width: 36px; text-align: center;
}
.b-r  { background: var(--r-bg);  color: var(--r-text); border: 1px solid var(--r-bord); }
.b-rw { background: #e5f0ff;      color: #004fb3;        border: 1px solid #9dbdee; }
.b-w  { background: var(--w-bg);  color: var(--w-text); border: 1px solid var(--w-bord); }

/* ── Hex value boxes ── */
.vwrap { display: flex; flex-wrap: wrap; gap: 3px; margin-top: 6px; }
.bx {
  display: inline-block; padding: 3px 8px; min-width: 32px;
  background: var(--surf2); border: 1px solid var(--border);
  border-radius: 4px; font-family: var(--mono); font-size: var(--fs-sm);
  color: var(--text); text-align: center; transition: all .35s;
}
.bx.live { background: #e4f7ec; border-color: #7ec89a; color: #1a6e38; }
.bx.err  { background: #fff2f2; border-color: #ffaaaa; color: var(--red); }

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
.rbtn { background: var(--r-bg); color: var(--r-text); border-color: var(--r-bord); }
.rbtn:hover { background: var(--r-hover); border-color: var(--r-text); }
.wbtn { background: var(--w-bg); color: var(--w-text); border-color: var(--w-bord); }
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
</style>
</head>
<body>

<header>
  <div class="hd-title">
    <h1>TimoTwo SPI Debugger</h1>
    <p>Radical Wireless LUMEN CONTROL &nbsp;·&nbsp; LumenRadio TimoTwo FX</p>
  </div>
  <div class="hd-ctrl">
    <div class="status">
      <div class="dot" id="dot"></div>
      <span id="stxt">–</span>
    </div>
    <button class="hbtn accent" onclick="readAll()">▶&nbsp; Read all</button>
    <button class="hbtn" onclick="clearVals()">✕&nbsp; Clear</button>
  </div>
</header>

<div id="main"></div>

<div id="logbar">
  <span id="logmsg">Ready.</span>
  <span class="log-meta">192.168.4.1 · TimoTwo-Debug</span>
</div>

<script>
// ── Register definitions ───────────────────────────────────────
const CATS = [
  { name: "Configuration", regs: [
    { a:0x00, n:"CONFIG", l:1, t:"rw",
      d:"Bit7=RADIO_EN &nbsp;Bit3=SPI_RDM &nbsp;Bit1=TX/RX_MODE &nbsp;Bit0=UART_EN" },
    { a:0x01, n:"STATUS", l:1, t:"rw",
      d:"Bit7=UPDATE_MODE &nbsp;Bit3=DMX &nbsp;Bit2=IDENTIFY(R/W: write 1=start identify) &nbsp;Bit1=RF_LINK(TX: write 1=start linking) &nbsp;Bit0=LINKED(write 1=unlink)" },
  ]},
  { name: "Interrupts", regs: [
    { a:0x02, n:"IRQ_MASK",      l:1, t:"rw",
      d:"Bit6=EXT_EN &nbsp;Bit5=IDENTIFY_EN &nbsp;Bit4=ASC_EN &nbsp;Bit3=RF_LINK_EN &nbsp;Bit2=DMX_CHG_EN &nbsp;Bit1=LOST_EN &nbsp;Bit0=RX_EN" },
    { a:0x03, n:"IRQ_FLAGS",     l:1, t:"r",
      d:"<b style='color:#c05400'>Bit7=SPI_BUSY(!)</b> &nbsp;Bit6=EXT &nbsp;Bit5=IDENTIFY &nbsp;Bit4=ASC &nbsp;Bit3=RF_LINK &nbsp;Bit2=DMX_CHG &nbsp;Bit1=LOST_DMX &nbsp;Bit0=RX_DMX" },
    { a:0x0A, n:"EXT_IRQ_MASK",  l:4, t:"rw",
      d:"B3: Bit7=NODE_QUERY_EN Bit5=RDM_DISC_EN Bit4=RADIO_MUTE_EN Bit3=RADIO_DISC_EN Bit2=RXTX_CTS_EN Bit1=RXTX_DA_EN Bit0=RDM_EN" },
    { a:0x0B, n:"EXT_IRQ_FLAGS", l:4, t:"rw",
      d:"Same layout as EXT_IRQ_MASK &nbsp;(write to clear flag)" },
  ]},
  { name: "DMX", regs: [
    { a:0x04, n:"DMX_WINDOW",  l:4, t:"rw",
      d:"Bytes 0-1: window size (max 512) &nbsp;Bytes 2-3: start address (0-indexed)" },
    { a:0x05, n:"ASC_FRAME",   l:3, t:"r",
      d:"Bytes 0-1: frame length &nbsp;Byte 2: start code (RX only)" },
    { a:0x08, n:"DMX_SPEC",    l:8, t:"rw",
      d:"Bytes 0-1: N_CHANNELS (1-512) &nbsp;Bytes 2-3: interslot µs &nbsp;Bytes 4-7: refresh period µs (25000 = 40 Hz)  TX only" },
    { a:0x09, n:"DMX_CONTROL", l:1, t:"rw",
      d:"Bit0=ENABLE &nbsp;Enables internal DMX generation from SPI buffer (TX only)" },
    { a:0x0D, n:"DMX_SOURCE",  l:1, t:"r",
      d:"Active source: 0=none &nbsp;1=UART &nbsp;2=wireless &nbsp;3=SPI &nbsp;4=BLE" },
  ]},
  { name: "RF / Radio", regs: [
    { a:0x06, n:"LINK_QUALITY",     l:1,  t:"r",
      d:"PDR 0–255 = 0–100% receive quality (RX only)" },
    { a:0x0C, n:"RF_PROTOCOL",      l:1,  t:"rw",
      d:"0=CRMX &nbsp;1=W-DMX G3 &nbsp;3=W-DMX G4S &nbsp;TX only · write triggers module restart" },
    { a:0x0E, n:"LOLLIPOP",         l:1,  t:"rw",
      d:"Restart detector: counter 0→255→128→… &nbsp;Value &lt;128 = fresh restart" },
    { a:0x11, n:"RF_POWER",         l:1,  t:"rw",
      d:"TX power: 2=20 dBm/100 mW &nbsp;3=16 dBm &nbsp;4=11 dBm &nbsp;5=5 dBm &nbsp;(TX only)" },
    { a:0x12, n:"BLOCKED_CHANNELS", l:11, t:"rw",
      d:"11 bytes: bit=1 blocks channel 2–80 (2402–2480 MHz) &nbsp;Min. 16 must stay active &nbsp;TX only" },
  ]},
  { name: "Linking", regs: [
    { a:0x20, n:"BINDING_UID", l:6,  t:"rw",
      d:"RDM binding UID of the host device (6 bytes, MSB first)" },
    { a:0x21, n:"LINKING_KEY", l:10, t:"w",
      d:"B0-7: 8-digit key &nbsp;B8: mode (0=CRMX Classic 1=CRMX2) &nbsp;B9: output 0-7 &nbsp;RX only · write triggers restart" },
  ]},
  { name: "BLE / Device", regs: [
    { a:0x30, n:"BLE_STATUS",  l:1,  t:"rw",
      d:"Bit1=PIN_ACTIVE (R) &nbsp;Bit0=BLE_ENABLE (RW) &nbsp;write triggers restart" },
    { a:0x31, n:"BLE_PIN",     l:6,  t:"w",
      d:"6 ASCII digits (0x30–0x39) &nbsp;0xFF×6 = disable PIN &nbsp;write triggers restart" },
    { a:0x32, n:"BATTERY",     l:1,  t:"w",
      d:"Battery level for BLE display: 0–100% &nbsp;255 = no battery" },
    { a:0x34, n:"OEM_INFO",    l:4,  t:"rw",
      d:"Bytes 0-1: device model ID &nbsp;Bytes 2-3: manufacturer ID (ESTA)" },
    { a:0x35, n:"RXTX_STATUS", l:1,  t:"r",
      d:"Bit1=CLEAR_TO_SEND &nbsp;Bit0=DATA_AVAILABLE &nbsp;(BLE RXTX service)" },
    { a:0x36, n:"DEVICE_NAME", l:32, t:"rw",
      d:"32 chars ASCII null-terminated = BLE device name = RDM DEVICE_LABEL = TX universe name (16 chars)" },
  ]},
  { name: "Universe", regs: [
    { a:0x33, n:"UNIVERSE_COLOR", l:3,  t:"rw",
      d:"RGB 3 bytes &nbsp;TX: transmitted to RX &nbsp;RX: reflects received color" },
    { a:0x37, n:"UNIVERSE_NAME",  l:16, t:"r",
      d:"16 chars ASCII null-terminated &nbsp;Universe name from TX (RX only)" },
  ]},
  { name: "Info", regs: [
    { a:0x10, n:"VERSION",           l:8,  t:"r",
      d:"Bytes 0-3: HW version (e.g. 0000A003) &nbsp;Bytes 4-7: FW version (e.g. 1.0.7.2)" },
    { a:0x3F, n:"PRODUCT_ID",        l:4,  t:"r",
      d:"TimoTwo = F1 40 00 00" },
    { a:0x3D, n:"INSTALLED_OPTIONS", l:13, t:"r",
      d:"Byte 0 = option count &nbsp;then 2 bytes each: 0x2001=RDM TX via SPI &nbsp;0x2002=RDM TX via proxy" },
  ]},
];

// ── Utilities ──────────────────────────────────────────────────
const hx = v => v.toString(16).toUpperCase().padStart(2,'0');
const rk = a => 'r'+a;

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
        const p = [];
        if (b0 & 0x01) p.push('LINKED');
        if (b0 & 0x02) p.push('RF-LINK');
        if (b0 & 0x04) p.push('IDENTIFY');
        if (b0 & 0x08) p.push('DMX active');
        if (b0 & 0x80) p.push('UPDATE MODE');
        return p.length ? p.join(' · ') : 'idle';
      }
      case 0x02: case 0x03: { // IRQ_MASK / IRQ_FLAGS
        const p = [];
        if (b0 & 0x80) p.push('SPI_BUSY');
        if (b0 & 0x40) p.push('EXT');
        if (b0 & 0x20) p.push('IDENTIFY');
        if (b0 & 0x10) p.push('ASC');
        if (b0 & 0x08) p.push('RF_LINK');
        if (b0 & 0x04) p.push('DMX_CHG');
        if (b0 & 0x02) p.push('LOST');
        if (b0 & 0x01) p.push('RX_DMX');
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
        return `${nCh} channels · ${hz} Hz · ${(b[2]<<8)|b[3]} µs interslot`;
      }
      case 0x09: // DMX_CONTROL
        return (b0 & 0x01) ? 'DMX output  ENABLED' : 'DMX output  DISABLED';
      case 0x0C: // RF_PROTOCOL
        return ['CRMX', 'W-DMX G3', '(reserved)', 'W-DMX G4S'][b0] || `0x${hx(b0)}`;
      case 0x0D: // DMX_SOURCE
        return (['none','UART','wireless','SPI','BLE'][b0] || `0x${hx(b0)}`) + ' source';
      case 0x0E: // LOLLIPOP
        return `counter: ${b0}${b0 < 128 ? '  — fresh restart' : ''}`;
      case 0x10: { // VERSION
        if (b.length < 8) return '';
        return `HW: ${hx(b[0])}${hx(b[1])}${hx(b[2])}${hx(b[3])}   FW: ${b[4]}.${b[5]}.${b[6]}.${b[7]}`;
      }
      case 0x11: // RF_POWER
        return ({2:'20 dBm  (100 mW)',3:'16 dBm',4:'11 dBm',5:'5 dBm'})[b0] || `0x${hx(b0)}`;
      case 0x20: // BINDING_UID
        return 'UID: ' + Array.from(b).map(hx).join(':');
      case 0x30: // BLE_STATUS
        return ((b0&0x01)?'BLE ON':'BLE OFF') + ((b0&0x02)?' · PIN active':'');
      case 0x33: // UNIVERSE_COLOR
        if (b.length < 3) return '';
        return `R: ${b[0]}  G: ${b[1]}  B: ${b[2]}`;
      case 0x34: { // OEM_INFO
        if (b.length < 4) return '';
        const model = (b[0]<<8)|b[1], esta = (b[2]<<8)|b[3];
        return `Model: 0x${model.toString(16).toUpperCase().padStart(4,'0')}   ESTA: 0x${esta.toString(16).toUpperCase().padStart(4,'0')}`;
      }
      case 0x35: // RXTX_STATUS
        return ((b0&0x02)?'CTS  ':'') + ((b0&0x01)?'DATA AVAILABLE':'no data');
      case 0x36: case 0x37: { // DEVICE_NAME / UNIVERSE_NAME
        const s = Array.from(b).map(v=>(v>=0x20&&v<0x7f)?String.fromCharCode(v):'\0').join('').split('\0')[0];
        return s.length ? `"${s}"` : '(empty)';
      }
      case 0x3D: { // INSTALLED_OPTIONS
        const cnt = b[0];
        if (!cnt) return 'no options installed';
        const opts = [];
        for (let i=0; i<cnt && (1+i*2+1)<b.length; i++) {
          const code = (b[1+i*2]<<8)|b[2+i*2];
          opts.push(({0x2001:'RDM TX via SPI',0x2002:'RDM TX proxy'})[code] || `0x${code.toString(16).toUpperCase()}`);
        }
        return opts.join(', ');
      }
      case 0x3F: // PRODUCT_ID
        return b.map(hx).join(' ') + ((b[0]===0xF1&&b[1]===0x40)?'   ✓ TimoTwo':'   ✗ unknown');
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
  for (let i=0; i<show; i++) h+='<span class="bx">--</span>';
  if (len>32) h+=`<span class="bx">+${len-32}</span>`;
  return h;
}

function badge(t) {
  return `<span class="badge b-${t}">${t.toUpperCase()}</span>`;
}

// ── Build a single register row ────────────────────────────────
function buildRow(reg) {
  const k   = rk(reg.a);
  const hr  = reg.t==='r'||reg.t==='rw';
  const hw  = reg.t==='w'||reg.t==='rw';
  const plen = Math.min(reg.l, 8);
  const ph  = Array.from({length:plen},()=>'XX').join(' ');

  const readBtn   = hr ? `<button class="rbtn" onclick="doRead(${reg.a},${reg.l})">R</button>` : '';
  const writeCtrl = hw
    ? `<input class="hex" id="inp-${k}" placeholder="${ph}" style="width:${Math.max(72, plen*30)}px">
       <button class="wbtn" onclick="doWrite(${reg.a},'${k}')">W</button>`
    : '';

  return `<tr>
    <td class="c-addr">0x${hx(reg.a)}</td>
    <td class="c-name">${reg.n}</td>
    <td class="c-type">${badge(reg.t)}</td>
    <td class="c-len">${reg.l}B</td>
    <td class="c-desc">${reg.d}</td>
    <td class="c-val">
      <div class="vwrap" id="val-${k}" data-addr="${reg.a}">${emptyVal(reg.l)}</div>
      <div class="vdec" id="dec-${k}"></div>
    </td>
    <td class="c-ctrl">
      <div class="ctrl">
        ${readBtn}${writeCtrl}
        <span class="msg" id="msg-${k}"></span>
      </div>
    </td>
  </tr>`;
}

// ── Render all categories ──────────────────────────────────────
function render() {
  let h = '';
  CATS.forEach((cat, ci) => {
    h += `<div class="cat">
      <div class="cat-hd" onclick="toggle(${ci})">
        <span class="cat-arrow" id="arr-${ci}">▼</span>
        <span class="cat-name">${cat.name}</span>
        <span class="cat-cnt">${cat.regs.length} registers</span>
      </div>
      <div class="cat-body" id="cat-${ci}">
        <div class="tbl-wrap"><table>
          <colgroup>
            <col style="width:66px"><col style="width:185px">
            <col style="width:44px"><col style="width:40px">
            <col><col style="min-width:160px;max-width:260px"><col style="width:0">
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
  for (let i=0; i<show; i++) h += `<span class="bx live">${hx(bytes[i])}</span>`;
  if (bytes.length > 32) h += `<span class="bx">+${bytes.length-32}</span>`;
  el.innerHTML = h;

  setTimeout(() => el.querySelectorAll('.bx').forEach(b => b.classList.remove('live')), 900);

  if (dec && !isNaN(addr)) dec.textContent = decodeReg(addr, bytes);
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
  setStatus('', '–');
  log('Cleared.');
}

// ── API: read single register ──────────────────────────────────
async function doRead(addr, len) {
  const k = rk(addr);
  setMsg(k, '…', '');
  try {
    const r = await fetch(`/api/read?addr=${addr}&len=${len}`);
    const j = await r.json();
    if (j.ok) {
      showVal(j.bytes, k);
      setMsg(k, '✓', 'ok');
      log(`READ  0x${hx(addr)}  →  ${j.bytes.map(hx).join(' ')}`, 'ok');
    } else {
      setMsg(k, '✗', 'err');
      log(`Read error  0x${hx(addr)}`, 'err');
    }
  } catch(e) {
    setMsg(k, '?', 'err');
    setStatus('err', 'Offline');
    log('Connection error', 'err');
  }
}

// ── API: write single register ─────────────────────────────────
async function doWrite(addr, key) {
  const inp = document.getElementById('inp-'+key);
  if (!inp) return;
  let hex = inp.value.replace(/\s+/g,'').replace(/[^0-9a-fA-F]/g,'');
  if (!hex.length || hex.length % 2 !== 0) {
    log('Invalid input — enter hex bytes, e.g.  82  or  00 01 08 B6', 'err');
    return;
  }
  setMsg(key, '…', '');
  try {
    const r = await fetch(`/api/write?addr=${addr}&data=${hex}`);
    const j = await r.json();
    if (j.ok) {
      setMsg(key, '✓', 'ok');
      log(`WRITE 0x${hx(addr)}  ←  ${hex.toUpperCase().match(/.{2}/g).join(' ')}`, 'ok');
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
    const r = await fetch('/api/readall');
    const j = await r.json();
    if (j.ok) {
      let ok=0, fail=0;
      Object.entries(j.registers).forEach(([aStr, v]) => {
        if (v.ok) { showVal(v.bytes, rk(+aStr)); ok++; }
        else {
          const el = document.getElementById('val-'+rk(+aStr));
          if (el) el.querySelectorAll('.bx').forEach(b=>{b.classList.add('err');b.textContent='??';});
          fail++;
        }
      });
      setStatus('ok', 'OK');
      log(`${ok} registers read${fail?' · '+fail+' error(s)':''}.`, 'ok');
    } else {
      setStatus('err', 'Error');
      log('readAll failed', 'err');
    }
  } catch(e) {
    setStatus('err', 'Offline');
    log('Connection error', 'err');
  }
}

// Init
render();
</script>
</body>
</html>
)rawliteral";

// ════════════════════════════════════════════════════════════════
//  setup() and loop()
// ════════════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println("\n=== TimoTwo SPI Debugger | LUMEN CONTROL ===");

    // Configure GPIO pins
    pinMode(PIN_CS,   OUTPUT);  digitalWrite(PIN_CS, HIGH);
    pinMode(PIN_IRQ,  INPUT_PULLUP);
    pinMode(PIN_LINK, INPUT_PULLUP);

    // Start VSPI bus
    timoSPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
    delay(300);
    Serial.println("[SPI] ready");

    // Start WiFi access point
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID);
    Serial.printf("[WiFi] AP: %s  IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

    // Register HTTP routes
    server.on("/",            HTTP_GET, []{ server.send_P(200, "text/html", INDEX_HTML); });
    server.on("/api/read",    HTTP_GET, handleRead);
    server.on("/api/write",   HTTP_GET, handleWrite);
    server.on("/api/readall", HTTP_GET, handleReadAll);
    server.onNotFound([]{ server.send(404, "text/plain", "Not found"); });

    server.begin();
    Serial.println("[HTTP] Server running at http://192.168.4.1");
}

void loop() {
    server.handleClient();
}

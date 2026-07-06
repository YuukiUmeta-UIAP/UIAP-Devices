/**
 * SDLog.ino — UART serial data logger for UIAPduino
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 tarosay
 *
 * Overview:
 *   Receives data on UART RX and writes it to a microSD card.
 *   A new log file is created on every power cycle.
 *
 * Log files:
 *   Files are created in order: LOG00001.TXT, LOG00002.TXT, ...
 *   Synced data is retained after power-off.
 *
 * Baud rate (CONFIG.TXT):
 *   Place CONFIG.TXT on the SD card to set the baud rate.
 *   Format: 9600,26,3,0  (only the first value is used)
 *   If the file does not exist, 9600 bps is used and CONFIG.TXT is
 *   created automatically. Baud rates of 300 bps or higher can be specified.
 *
 * LED behavior:
 *   Fast blink  — error (SD init failed, cannot create log file, etc.)
 *   Blink       — writing data to SD
 *   Steady      — waiting for data
 *
 * Board and IDE settings:
 *   Board Manager URL:
 *     https://github.com/tarosay/board_manager_files/raw/main/package_uiap_hid_index.json
 *   Board : Tools > Board > UIAP_HID > HID ProMicro CH32V003
 *   USB   : Tools > USB > No USB (SD log / UART only)
 *   FQBN  : UIAP_HID:ch32v:CH32V003:usb=nousb,opt=oslto
 *
 * Wiring — UIAPduino to microSD card adapter:
 *
 *   UIAPduino        SD card adapter
 *   ─────────────────────────────────
 *   A2  (PC4, pin 6)  →  CS   (DAT3)
 *   8   (PC6)         →  MOSI (CMD / DI)
 *   7   (PC5)         →  SCK  (CLK)
 *   9   (PC7)         →  MISO (DAT0 / DO)
 *   3V3               →  VDD
 *   GND               →  VSS
 *
 * UART (data input):
 *   RX  PD6  — connect to TX of the device to be logged
 *   TX  PD5  — not used for logging (available for debug output)
 */

#include <Arduino.h>
#include <SDmin.h>
#include "UIAPSerial.h"

#define LED_BUILTIN 2
static const uint8_t  PIN_SS   = 6;   // CS = A2 = PC4
static const uint8_t  BUF_SIZE = 64;
static const uint32_t IDLE_MS  = 200;   // flush after this many ms of inactivity

static uint8_t  rxBuf[BUF_SIZE];
static uint8_t  rxLen      = 0;
static uint32_t lastRxTime = 0;
static char     logFilename[13];   // "LOG00001.TXT\0"

// ── Read baud rate from CONFIG.TXT (create with default if missing) ───────────
static const char CONFIG_DEFAULT[] = "9600,26,3,0\r\n";

static uint32_t load_config(void) {
  // File not found → create with default value and return 9600
  if (!sm_open_r("CONFIG.TXT")) {
    if (sm_open_w("CONFIG.TXT")) {
      sm_write((const uint8_t*)CONFIG_DEFAULT, sizeof(CONFIG_DEFAULT) - 1);
      sm_close_w();
    }
    return 9600UL;
  }

  // File found → read the first field (baud rate)
  char buf[8];
  uint8_t n = 0;
  uint8_t b;
  while (n < (uint8_t)(sizeof(buf) - 1)) {
    if (sm_read(&b, 1) != 1) break;
    if ((char)b == ',' || (char)b == '\r' || (char)b == '\n') break;
    buf[n++] = (char)b;
  }
  sm_close_r();
  buf[n] = '\0';

  uint32_t baud = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (buf[i] >= '0' && buf[i] <= '9')
      baud = baud * 10 + (uint32_t)(buf[i] - '0');
  }
  return (baud >= 300UL) ? baud : 9600UL;
}

// ── Utilities ─────────────────────────────────────────────────────────────────
static void error_blink(void) {
  while (1) {
    digitalWrite(LED_BUILTIN, HIGH); delay(200);
    digitalWrite(LED_BUILTIN, LOW);  delay(200);
  }
}

static void make_logname(char *buf, uint16_t n) {
  buf[0]='L'; buf[1]='O'; buf[2]='G';
  buf[3] = '0' + (n / 10000) % 10;
  buf[4] = '0' + (n /  1000) % 10;
  buf[5] = '0' + (n /   100) % 10;
  buf[6] = '0' + (n /    10) % 10;
  buf[7] = '0' + (n        ) % 10;
  buf[8]='.'; buf[9]='T'; buf[10]='X'; buf[11]='T'; buf[12]='\0';
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  if (!sm_init(PIN_SS)) error_blink();
  if (!sm_mount())      error_blink();

  uint32_t baud = load_config();

  // Find the first unused filename and open it for writing (stays open until power-off)
  // uart.begin() is called AFTER the file is open so no SPI operation occurs during
  // UART reception.
  bool found = false;
  for (uint16_t i = 1; i <= 65534u; i++) {
    make_logname(logFilename, i);
    if (!sm_open_r(logFilename)) {
      found = sm_open_w(logFilename);
      break;
    }
    sm_close_r();
  }
  if (!found) error_blink();

  uart.begin(baud);
}

// ── Loop: UART → SD ───────────────────────────────────────────────────────────
//
// Design note — open once in setup(), sync at each burst end:
//   The file is opened by sm_open_w() in setup() before uart.begin().
//   No SPI operation ever occurs while UART reception is active.
//
//   sm_sync_w() is called after each burst (IDLE_MS of inactivity) or when
//   rxBuf is full. It flushes the partial sector and updates the directory
//   size in ~4 ms regardless of file length (no FAT chain walk).
//
//   On power cut, at most BUF_SIZE (64) bytes currently in rxBuf may be lost.
//   All data already passed to sm_sync_w() is committed to the SD card.
//
void loop() {
  static uint8_t ledSt = 0;
  bool got = false;

  while (uart.available()) {
    if (rxLen < BUF_SIZE) {
      rxBuf[rxLen++] = uart.read();
      got = true;
    } else {
      break;
    }
  }

  if (got) lastRxTime = millis();

  // Buffer full → write only (UART may still be active; sm_sync_w must not run here)
  if (rxLen >= BUF_SIZE) {
    sm_write(rxBuf, rxLen); rxLen = 0;
    ledSt ^= 1; digitalWrite(LED_BUILTIN, ledSt);
    return;
  }

  // Idle for IDLE_MS → write buffered data and sync
  if (rxLen > 0 && (uint32_t)(millis() - lastRxTime) >= IDLE_MS) {
    sm_write(rxBuf, rxLen); rxLen = 0;
    sm_sync_w();   // ~4 ms: flush sector + update dir size
    ledSt ^= 1; digitalWrite(LED_BUILTIN, ledSt);
  }
}

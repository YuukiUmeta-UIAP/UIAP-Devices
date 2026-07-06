#pragma once
#include <stdint.h>

/**
 * UIAPSerial — USART1 serial driver for UIAPduino (CH32V003)
 *
 * Pins:
 *   RX  PD6  — receive
 *   TX  PD5  — transmit
 *
 * Usage:
 *   uart.begin(9600);            // initialize at the given baud rate
 *   if (uart.available()) {      // returns 1 if data is waiting
 *     uint8_t b = uart.read();   // read one byte
 *   }
 *   uart.write('A');             // send one byte
 *   uart.print("hello");         // send a string
 *   uart.println("world");       // send a string followed by CR+LF
 */
class UIAPSerial {
public:
  void    begin(uint32_t baud);

  // RX
  uint8_t available();
  uint8_t read();

  // TX
  void    write(uint8_t b);
  void    print(const char* s);
  void    println(const char* s = "");
};

extern UIAPSerial uart;

#pragma once
#include "types.h"
#include "x86.h"

// COM1 base port
#define SERIAL_COM1        0x3F8
#define SERIAL_COM2        0x2F8

// UART register offsets (from base port)
#define UART_DATA          0    // Data register (R/W)
#define UART_IER           1    // Interrupt Enable Register
#define UART_IIR           2    // Interrupt Identification Register (read)
#define UART_FCR           2    // FIFO Control Register (write)
#define UART_LCR           3    // Line Control Register
#define UART_MCR           4    // Modem Control Register
#define UART_LSR           5    // Line Status Register
#define UART_MSR           6    // Modem Status Register

// Divisor Latch (when DLAB=1)
#define UART_DLL           0    // Divisor Latch Low byte
#define UART_DLH           1    // Divisor Latch High byte

// Line Control Register bits
#define UART_LCR_DLAB      0x80  // Divisor Latch Access Bit
#define UART_LCR_8N1       0x03  // 8 data bits, no parity, 1 stop bit

// Line Status Register bits
#define UART_LSR_THRE      0x20  // Transmitter Holding Register Empty

// FIFO Control Register bits
#define UART_FCR_ENABLE    0xC7  // Enable FIFOs, clear, 14-byte threshold

// Modem Control Register bits
#define UART_MCR_DTR       0x01  // Data Terminal Ready
#define UART_MCR_RTS       0x02  // Request To Send
#define UART_MCR_OUT2      0x08  // Aux Output 2 (IRQ enable)
#define UART_MCR_LOOP      0x10  // Loopback mode

// Default serial port
#define PORT SERIAL_COM1

// Divisor for 115200 baud (1843200 / 16 / 115200 = 1... but use 3 for 38400)
#define UART_BAUD_DIVISOR  3

// Serial loopback test byte
#define UART_LOOPBACK_TEST 0xAE

void init_serial(void);
u32  is_transmit_empty(void);
void serial_putc(char c);
void serial_puts(char *s);

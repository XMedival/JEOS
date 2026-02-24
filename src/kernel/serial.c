#include "serial.h"

void init_serial(void) {
    outb(PORT + UART_IER, 0x00);              // Disable all interrupts
    outb(PORT + UART_LCR, UART_LCR_DLAB);     // Enable DLAB
    outb(PORT + UART_DLL, UART_BAUD_DIVISOR); // Set divisor low byte
    outb(PORT + UART_DLH, 0x00);              // Set divisor high byte
    outb(PORT + UART_LCR, UART_LCR_8N1);      // 8 bits, no parity, one stop bit
    outb(PORT + UART_FCR, UART_FCR_ENABLE);   // Enable FIFO, clear, 14-byte threshold
    outb(PORT + UART_MCR, UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2);
    outb(PORT + UART_MCR, UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2 | UART_MCR_LOOP);
    outb(PORT + UART_DATA, UART_LOOPBACK_TEST);
    // Note: loopback test could check inb(PORT) == UART_LOOPBACK_TEST
    outb(PORT + UART_MCR, UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2);
}

u32 is_transmit_empty(void) {
    return inb(PORT + UART_LSR) & UART_LSR_THRE;
}

void serial_putc(char c) {
    while (is_transmit_empty() == 0)
        ;
    outb(PORT + UART_DATA, c);
}

void serial_puts(char *s) {
    for (u32 i = 0; s[i] != '\0'; i++) {
        serial_putc(s[i]);
    }
}

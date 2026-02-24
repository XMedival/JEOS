#pragma once

// PIT (Programmable Interval Timer) constants
#define PIT_FREQ      1193182   // Base frequency in Hz
#define PIT_CMD       0x43      // Command register
#define PIT_CH0       0x40      // Channel 0 data port
#define PIT_CH1       0x41      // Channel 1 data port
#define PIT_CH2       0x42      // Channel 2 data port

// PIT command byte format: CCAA_MMMB
// CC = channel (00=0, 01=1, 10=2, 11=read-back)
// AA = access mode (00=latch, 01=lobyte, 10=hibyte, 11=lo/hibyte)
// MMM = mode (000-101)
// B = BCD mode (0=binary, 1=BCD)

// Mode bits
#define PIT_MODE_INTTERM     0x00  // Mode 0: Interrupt on terminal count
#define PIT_MODE_ONESHOT     0x02  // Mode 1: Hardware re-triggerable one-shot
#define PIT_MODE_RATEGEN     0x04  // Mode 2: Rate generator
#define PIT_MODE_SQUARE      0x06  // Mode 3: Square wave generator
#define PIT_MODE_SWSTROBE    0x08  // Mode 4: Software triggered strobe
#define PIT_MODE_HWSTROBE    0x0A  // Mode 5: Hardware triggered strobe

// Access mode bits
#define PIT_ACCESS_LATCH     0x00
#define PIT_ACCESS_LOBYTE    0x10
#define PIT_ACCESS_HIBYTE    0x20
#define PIT_ACCESS_LOHIBYTE  0x30

// Channel select bits
#define PIT_CH0_SELECT       0x00
#define PIT_CH1_SELECT       0x40
#define PIT_CH2_SELECT       0x80

// Common combined commands
#define PIT_CMD_CH0_SQUARE   (PIT_CH0_SELECT | PIT_ACCESS_LOHIBYTE | PIT_MODE_SQUARE)     // 0x36
#define PIT_CMD_CH0_ONESHOT  (PIT_CH0_SELECT | PIT_ACCESS_LOHIBYTE | PIT_MODE_INTTERM)    // 0x30

#pragma once
#include "types.h"
#include "blk.h"

// ATA I/O base ports
#define ATA_PRIMARY_BASE    0x1F0
#define ATA_SECONDARY_BASE  0x170
#define ATA_PRIMARY_CTRL    0x3F6
#define ATA_SECONDARY_CTRL  0x376

// ATA register offsets from base
#define ATA_REG_DATA        0x00
#define ATA_REG_ERROR       0x01
#define ATA_REG_FEATURES    0x01
#define ATA_REG_SECCOUNT0   0x02
#define ATA_REG_LBA0        0x03
#define ATA_REG_LBA1        0x04
#define ATA_REG_LBA2        0x05
#define ATA_REG_HDDEVSEL    0x06
#define ATA_REG_COMMAND     0x07
#define ATA_REG_STATUS      0x07
#define ATA_REG_SECCOUNT1   0x02  // LBA48: same port as SECCOUNT0, written first
#define ATA_REG_LBA3        0x03  // LBA48: same port as LBA0
#define ATA_REG_LBA4        0x04  // LBA48: same port as LBA1
#define ATA_REG_LBA5        0x05  // LBA48: same port as LBA2

// ATA Status bits
#define ATA_SR_BSY          0x80
#define ATA_SR_DRDY         0x40
#define ATA_SR_DRQ          0x08
#define ATA_SR_ERR          0x01

// ATA Commands
#define ATA_CMD_READ_DMA_EXT    0x25
#define ATA_CMD_WRITE_DMA_EXT   0x35
#define ATA_CMD_IDENTIFY        0xEC

// Bus Master IDE register offsets (from BAR4 base)
#define BM_CMD      0x00   // Bus Master Command
#define BM_STATUS   0x02   // Bus Master Status
#define BM_PRDT     0x04   // PRD Table Address

// Bus Master Command bits
#define BM_CMD_START    (1 << 0)
#define BM_CMD_READ     (1 << 3)  // 1 = read from disk (write to memory)

// Bus Master Status bits
#define BM_STATUS_ACTIVE    (1 << 0)
#define BM_STATUS_ERROR     (1 << 1)
#define BM_STATUS_IRQ       (1 << 2)
#define BM_STATUS_DRV0_DMA  (1 << 5)
#define BM_STATUS_DRV1_DMA  (1 << 6)

// PRD Table Entry
struct ata_prd {
    u32 base;      // Physical base address
    u16 count;     // Byte count (0 = 64 KiB)
    u16 eot;       // Bit 15 = End-of-Table
} __attribute__((packed));

#define ATA_PRD_EOT     (1 << 15)

void ata_init(void);
void ata_irq_handler(int channel);  // channel: 0 = primary, 1 = secondary

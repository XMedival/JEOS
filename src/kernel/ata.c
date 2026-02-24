#include "ata.h"
#include "apic.h"
#include "blk.h"
#include "mem.h"
#include "pci.h"
#include "print.h"
#include "x86.h"

// One channel state (primary or secondary)
struct ata_channel {
    u16 base;        // I/O base (0x1F0 or 0x170)
    u16 ctrl;        // Control port (0x3F6 or 0x376)
    u16 bm_base;     // Bus master base (BAR4 + 0 or + 8)
    struct ata_prd *prd;     // PRD table (one entry, page-allocated)
    u64             prd_phys;
    struct blk_device *blk;  // Registered block device (NULL if no drive)
};

static struct ata_channel channels[2];

// ============================================================================
// Helpers
// ============================================================================

static void ata_wait_bsy(struct ata_channel *ch) {
    while (inb(ch->ctrl) & ATA_SR_BSY);
}

// ============================================================================
// DMA submit (called by blk layer)
// ============================================================================

static int ata_submit(struct blk_device *dev, struct blk_request *req) {
    // Find which channel owns this device
    struct ata_channel *ch = (struct ata_channel *)dev->priv;

    u64 lba    = req->lba;
    u32 count  = req->count;
    u64 buf_pa = VIRT_TO_PHYS((u64)req->buf);
    u32 bytes  = count * dev->sector_size;

    // Fill PRD (single entry, up to 64 KiB per transfer)
    ch->prd->base  = (u32)buf_pa;
    ch->prd->count = (u16)(bytes == 65536 ? 0 : bytes);
    ch->prd->eot   = ATA_PRD_EOT;

    // Reset bus master: stop, clear status (preserve DMA-capable bits)
    outb(ch->bm_base + BM_CMD, 0);
    outb(ch->bm_base + BM_STATUS,
         inb(ch->bm_base + BM_STATUS) | BM_STATUS_ERROR | BM_STATUS_IRQ);

    // Load PRD table address
    outl(ch->bm_base + BM_PRDT, (u32)ch->prd_phys);

    // Set bus master direction
    u8 bm_cmd = req->write ? 0 : BM_CMD_READ;
    outb(ch->bm_base + BM_CMD, bm_cmd);

    // Select drive 0, LBA48 mode
    ata_wait_bsy(ch);
    outb(ch->base + ATA_REG_HDDEVSEL, 0xE0);  // master, LBA (bits 7,6,5 required)

    // Write high bytes of LBA48 count and address
    outb(ch->base + ATA_REG_SECCOUNT1, (u8)(count >> 8));
    outb(ch->base + ATA_REG_LBA3, (u8)(lba >> 24));
    outb(ch->base + ATA_REG_LBA4, (u8)(lba >> 32));
    outb(ch->base + ATA_REG_LBA5, (u8)(lba >> 40));

    // Write low bytes
    outb(ch->base + ATA_REG_SECCOUNT0, (u8)count);
    outb(ch->base + ATA_REG_LBA0, (u8)lba);
    outb(ch->base + ATA_REG_LBA1, (u8)(lba >> 8));
    outb(ch->base + ATA_REG_LBA2, (u8)(lba >> 16));

    // Mask IRQ for this channel while we poll to prevent spurious delivery
    u8 irq = (ch == &channels[0]) ? 14 : 15;
    ioapic_mask_irq(irq);

    // Issue command
    u8 cmd = req->write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT;
    outb(ch->base + ATA_REG_COMMAND, cmd);

    // Start bus master DMA
    outb(ch->bm_base + BM_CMD, bm_cmd | BM_CMD_START);

    // Poll BM_STATUS until transfer complete (IRQ or error bit set)
    u8 bm_status;
    do {
        bm_status = inb(ch->bm_base + BM_STATUS);
    } while (!(bm_status & (BM_STATUS_IRQ | BM_STATUS_ERROR)));

    // Stop DMA and clear status
    outb(ch->bm_base + BM_CMD, 0);
    outb(ch->bm_base + BM_STATUS, bm_status);  // write-1-to-clear IRQ/error
    inb(ch->base + ATA_REG_STATUS);             // clear ATA interrupt line

    // Send EOI so LAPIC doesn't re-deliver the now-consumed IRQ, then re-enable
    lapic_eoi();
    ioapic_unmask_irq(irq);

    i32 err = (bm_status & BM_STATUS_ERROR) ? -1 : 0;
    blk_complete(dev, err);
    return 0;
}

// ============================================================================
// IRQ handler (called from idt.c)
// ============================================================================

void ata_irq_handler(int channel) {
    struct ata_channel *ch = &channels[channel];

    // Ack bus master interrupt
    u8 status = inb(ch->bm_base + BM_STATUS);
    outb(ch->bm_base + BM_STATUS, status);  // write-back to clear IRQ bit

    // Stop bus master
    outb(ch->bm_base + BM_CMD, 0);

    // Read ATA status to clear interrupt
    inb(ch->base + ATA_REG_STATUS);

    i32 err = (status & BM_STATUS_ERROR) ? -1 : 0;

    if (ch->blk)
        blk_complete(ch->blk, err);
}

// ============================================================================
// IDENTIFY (polling, run before sti)
// ============================================================================

static int ata_identify_poll(struct ata_channel *ch) {
    ata_wait_bsy(ch);
    outb(ch->base + ATA_REG_HDDEVSEL, 0xA0);  // select master

    // 400ns delay: read alt-status 4 times after device select
    inb(ch->ctrl); inb(ch->ctrl); inb(ch->ctrl); inb(ch->ctrl);

    outb(ch->base + ATA_REG_SECCOUNT0, 0);
    outb(ch->base + ATA_REG_LBA0, 0);
    outb(ch->base + ATA_REG_LBA1, 0);
    outb(ch->base + ATA_REG_LBA2, 0);
    outb(ch->base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    // 400ns delay after command write
    inb(ch->ctrl); inb(ch->ctrl); inb(ch->ctrl); inb(ch->ctrl);

    u8 status = inb(ch->ctrl);
    if (!status || status == 0xFF) return -1;  // No drive

    while (inb(ch->ctrl) & ATA_SR_BSY);

    // Check LBA1/LBA2 — non-zero means not ATA (ATAPI etc.)
    if (inb(ch->base + ATA_REG_LBA1) || inb(ch->base + ATA_REG_LBA2))
        return -1;

    // Wait for DRQ or ERR
    while (1) {
        status = inb(ch->base + ATA_REG_STATUS);
        if (status & ATA_SR_DRQ) break;
        if (status & ATA_SR_ERR) return -1;
    }

    // Read 256 words
    u16 buf[256];
    for (int i = 0; i < 256; i++)
        buf[i] = inw(ch->base + ATA_REG_DATA);

    // Sector count from words 100-103 (LBA48)
    u64 sector_count = *(u64 *)&buf[100];
    u32 sector_size  = 512;  // ATA default; word 106 parsing omitted for brevity

    return 0;
}

// ============================================================================
// Init
// ============================================================================

void ata_init(void) {
    // Find PCI IDE controller
    struct pci_device *ide_dev = 0;
    for (u32 i = 0; i < pci_device_count; i++) {
        struct pci_device *d = &pci_devices[i];
        if (d->hdr.general.h.class_code == PCI_CLASS_STORAGE &&
            d->hdr.general.h.subclass   == PCI_SUBCLASS_IDE) {
            ide_dev = d;
            break;
        }
    }

    if (!ide_dev) {
        klog("ATA", "no IDE controller found");
        return;
    }

    klog("ATA", "IDE controller at %u:%u", ide_dev->bus, ide_dev->slot);

    // Enable bus master (bit 2 of command register)
    u16 cmd = pci_read16(ide_dev->bus, ide_dev->slot, ide_dev->func, 0x04);
    pci_write16(ide_dev->bus, ide_dev->slot, ide_dev->func, 0x04, cmd | 0x04);

    // BAR4 = bus master base (low 16 bits are I/O port, mask bit 0)
    u32 bar4 = pci_read32(ide_dev->bus, ide_dev->slot, ide_dev->func,
                           PCI_BAR0_OFFSET + 4 * 4);
    u16 bm_base = (u16)(bar4 & ~0x3U);

    // Set up channels
    channels[0].base    = ATA_PRIMARY_BASE;
    channels[0].ctrl    = ATA_PRIMARY_CTRL;
    channels[0].bm_base = bm_base;

    channels[1].base    = ATA_SECONDARY_BASE;
    channels[1].ctrl    = ATA_SECONDARY_CTRL;
    channels[1].bm_base = bm_base + 8;

    for (int i = 0; i < 1; i++) {
        struct ata_channel *ch = &channels[i];
        ch->blk = 0;

        // Allocate PRD table (must be page-aligned, below 4 GiB)
        ch->prd = (struct ata_prd *)kalloc(1);
        if (!ch->prd) continue;

        ch->prd_phys = VIRT_TO_PHYS((u64)ch->prd);

        // Enable bus master DMA capable bit in status
        outb(ch->bm_base + BM_STATUS,
             inb(ch->bm_base + BM_STATUS) | BM_STATUS_DRV0_DMA);

        if (ata_identify_poll(ch) != 0) {
            klog("ATA", "no drive on channel %d", i);
            continue;
        }

        char name[BLK_NAME_LEN] = "ata0";
        name[3] = '0' + i;

        struct blk_ops ops = { .submit = ata_submit };
        ch->blk = blk_register(name, ops, 512, ch);

        if (ch->blk)
            klog_ok("ATA", "disk %s", name);
    }
}

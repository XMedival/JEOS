#include "ahci.h"
#include "blk.h"
#include "pci.h"
#include "mem.h"
#include "print.h"
#include "x86.h"

// Global state
static struct pci_device *ahci_dev;
static struct hba_mem *hba;
struct hba_port *ahci_sata_port;       // First SATA port (for convenience)
struct ahci_port_info ahci_sata_info;  // Info for first SATA port

// Per-port allocated structures (32 ports max)
static struct hba_cmd_header *cmd_lists[32];
static struct hba_received_fis *fis_areas[32];
static struct hba_cmd_table *cmd_tables[32][32];  // [port][slot]
static struct ahci_port_info port_info[32];       // Per-port device info
static struct blk_device *port_blk[32];           // Block device per port

// ============================================================================
// Port Control
// ============================================================================

void ahci_port_stop(struct hba_port *port) {
    // Clear ST (stop command processing)
    port->cmd &= ~HBA_PORT_CMD_ST;

    // Wait for CR to clear
    while (port->cmd & HBA_PORT_CMD_CR)
        ;

    // Clear FRE
    port->cmd &= ~HBA_PORT_CMD_FRE;

    // Wait for FR to clear
    while (port->cmd & HBA_PORT_CMD_FR)
        ;
}

void ahci_port_start(struct hba_port *port) {
    // Wait for CR to clear
    while (port->cmd & HBA_PORT_CMD_CR)
        ;

    // Enable FRE and ST
    port->cmd |= HBA_PORT_CMD_FRE;
    port->cmd |= HBA_PORT_CMD_ST;
}

void ahci_port_reset(struct hba_port *port) {
    ahci_port_stop(port);

    // Issue COMRESET via SCTL
    port->sctl = (port->sctl & ~SCTL_DET_MASK) | SCTL_DET_COMRESET;

    // Wait a bit (spec says at least 1ms)
    for (volatile int i = 0; i < 100000; i++)
        ;

    port->sctl &= ~SCTL_DET_MASK;  // DET = 0 (no action)

    // Wait for device to come back
    while (HBA_PORT_SSTS_DET(port->ssts) != HBA_PORT_SSTS_DET_PRESENT)
        ;

    // Clear errors
    port->serr = port->serr;

    ahci_port_start(port);
}

// ============================================================================
// Port Initialization
// ============================================================================

static int get_port_num(struct hba_port *port) {
    return ((u64)port - (u64)&hba->ports[0]) / sizeof(struct hba_port);
}

int ahci_port_init(struct hba_port *port) {
    int port_num = get_port_num(port);

    ahci_port_stop(port);

    // Allocate command list (1KB, 1KB aligned)
    struct hba_cmd_header *cmd_list = kalloc(1);
    if (!cmd_list) return -1;
    memset(cmd_list, 0, PAGE_SIZE);
    cmd_lists[port_num] = cmd_list;

    u64 cmd_list_phys = VIRT_TO_PHYS((u64)cmd_list);
    port->clb = (u32)cmd_list_phys;
    port->clbu = (u32)(cmd_list_phys >> 32);

    // Allocate received FIS area (256 bytes, 256 aligned)
    struct hba_received_fis *fis = kalloc(1);
    if (!fis) return -1;
    memset(fis, 0, PAGE_SIZE);
    fis_areas[port_num] = fis;

    u64 fis_phys = VIRT_TO_PHYS((u64)fis);
    port->fb = (u32)fis_phys;
    port->fbu = (u32)(fis_phys >> 32);

    // Allocate command tables (one per slot, 128-byte aligned)
    for (int slot = 0; slot < 32; slot++) {
        struct hba_cmd_table *tbl = kalloc(1);
        if (!tbl) return -1;
        memset(tbl, 0, PAGE_SIZE);
        cmd_tables[port_num][slot] = tbl;

        u64 tbl_phys = VIRT_TO_PHYS((u64)tbl);
        cmd_list[slot].ctba = (u32)tbl_phys;
        cmd_list[slot].ctbau = (u32)(tbl_phys >> 32);
    }

    // Clear pending interrupts
    port->is = port->is;
    port->serr = port->serr;

    ahci_port_start(port);
    return 0;
}

// ============================================================================
// Device Detection
// ============================================================================

int ahci_port_type(struct hba_port *port) {
    u32 ssts = port->ssts;
    u8 det = HBA_PORT_SSTS_DET(ssts);
    u8 ipm = HBA_PORT_SSTS_IPM(ssts);

    if (det != HBA_PORT_SSTS_DET_PRESENT) return AHCI_DEV_NULL;  // No device
    if (ipm != HBA_PORT_SSTS_IPM_ACTIVE) return AHCI_DEV_NULL;   // Not active

    switch (port->sig) {
    case SATA_SIG_ATA:   return AHCI_DEV_SATA;
    case SATA_SIG_ATAPI: return AHCI_DEV_SATAPI;
    case SATA_SIG_SEMB:  return AHCI_DEV_SEMB;
    case SATA_SIG_PM:    return AHCI_DEV_PM;
    default:             return AHCI_DEV_NULL;
    }
}

// ============================================================================
// Command Slot Management
// ============================================================================

int ahci_find_slot(struct hba_port *port) {
    u32 slots = port->sact | port->ci;
    for (int i = 0; i < 32; i++) {
        if (!(slots & (1 << i)))
            return i;
    }
    return -1;
}

int ahci_issue_poll(struct hba_port *port, int slot) {
    // Wait for port to be ready
    while (port->tfd & (HBA_PORT_TFD_BSY | HBA_PORT_TFD_DRQ))
        ;

    // Issue command
    port->ci = (1 << slot);

    // Poll for completion
    while (1) {
        if (!(port->ci & (1 << slot)))
            break;
        if (port->is & HBA_PORT_IS_TFES)
            return -1;
    }

    if (port->is & HBA_PORT_IS_TFES)
        return -1;

    return 0;
}

// Non-blocking submit: caller waits via blk_submit_sync / blk_complete
void ahci_submit_dma(struct hba_port *port, int slot) {
    while (port->tfd & (HBA_PORT_TFD_BSY | HBA_PORT_TFD_DRQ))
        ;
    port->ci = (1 << slot);
}

// ============================================================================
// Sector Size Detection
// ============================================================================

void ahci_parse_identify(u8 *id, struct ahci_port_info *info) {
    // Parse model string (words 27-46, byte-swapped)
    for (int j = 0; j < 40; j += 2) {
        info->model[j] = id[ATA_IDENTIFY_MODEL_OFFSET + j + 1];
        info->model[j + 1] = id[ATA_IDENTIFY_MODEL_OFFSET + j];
    }
    info->model[40] = 0;
    // Trim trailing spaces
    for (int j = 39; j >= 0 && info->model[j] == ' '; j--)
        info->model[j] = 0;

    // Parse LBA48 sector count (words 100-103)
    info->sector_count = *(u64 *)&id[ATA_IDENTIFY_LBA48_OFFSET];

    // Parse sector size from word 106
    u16 w106 = *(u16 *)&id[ATA_ID_SECTOR_SIZE * 2];

    if ((w106 & ATA_ID_W106_VALID) && !(w106 & (1 << 15))) {
        // Word 106 is valid
        if (w106 & ATA_ID_W106_LOGICAL_GT512) {
            // Logical sector size > 512 bytes, read from words 117-118
            u32 logical_words = *(u32 *)&id[ATA_ID_LOGICAL_SIZE * 2];
            info->sector_size = logical_words * 2;  // Convert words to bytes
        } else {
            info->sector_size = ATA_SECTOR_SIZE_DEFAULT;
        }
    } else {
        // Word 106 not valid, assume 512 bytes
        info->sector_size = ATA_SECTOR_SIZE_DEFAULT;
    }
}

u32 ahci_get_sector_size(struct hba_port *port) {
    int port_num = get_port_num(port);
    return port_info[port_num].sector_size;
}

// ============================================================================
// IDENTIFY Command
// ============================================================================

int ahci_identify(struct hba_port *port, void *buf) {
    int port_num = get_port_num(port);

    ahci_port_clear_interrupts(port);

    int slot = ahci_find_slot(port);
    if (slot < 0) return -1;

    struct hba_cmd_header *hdr = &cmd_lists[port_num][slot];
    hdr->cfl = sizeof(struct fis_reg_h2d) / 4;
    hdr->w = 0;  // Read
    hdr->prdtl = 1;

    struct hba_cmd_table *tbl = cmd_tables[port_num][slot];

    // Setup PRDT
    u64 buf_phys = VIRT_TO_PHYS((u64)buf);
    tbl->prdt[0].dba = (u32)buf_phys;
    tbl->prdt[0].dbau = (u32)(buf_phys >> 32);
    tbl->prdt[0].dbc = 511;  // 512 bytes - 1
    tbl->prdt[0].i = 1;

    // Setup command FIS
    struct fis_reg_h2d *fis = (struct fis_reg_h2d *)tbl->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->flags = FIS_H2D_CMD;
    fis->command = ATA_CMD_IDENTIFY;
    fis->device = 0;

    return ahci_issue_poll(port, slot);
}

// ============================================================================
// Read/Write (via blk layer)
// ============================================================================

// blk_ops.submit callback: set up PRDT/FIS and fire non-blocking
static int ahci_submit(struct blk_device *dev, struct blk_request *req) {
    struct hba_port *port = (struct hba_port *)dev->priv;
    int port_num = get_port_num(port);
    u32 sector_size = port_info[port_num].sector_size;
    if (sector_size == 0) sector_size = ATA_SECTOR_SIZE_DEFAULT;

    ahci_port_clear_interrupts(port);

    int slot = ahci_find_slot(port);
    if (slot < 0) return -1;

    struct hba_cmd_header *hdr = &cmd_lists[port_num][slot];
    hdr->cfl   = sizeof(struct fis_reg_h2d) / 4;
    hdr->w     = req->write;
    hdr->prdtl = 1;

    struct hba_cmd_table *tbl = cmd_tables[port_num][slot];
    u64 buf_phys = VIRT_TO_PHYS((u64)req->buf);
    tbl->prdt[0].dba  = (u32)buf_phys;
    tbl->prdt[0].dbau = (u32)(buf_phys >> 32);
    tbl->prdt[0].dbc  = (req->count * sector_size) - 1;
    tbl->prdt[0].i    = 1;

    struct fis_reg_h2d *fis = (struct fis_reg_h2d *)tbl->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->flags    = FIS_H2D_CMD;
    fis->command  = req->write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT;
    fis_set_lba48(fis, req->lba);
    fis->device = ATA_DEV_LBA;
    fis->count  = (u16)req->count;

    ahci_submit_dma(port, slot);
    return 0;
}

// ============================================================================
// IRQ handler (called from vector 48 — AHCI MSI)
// ============================================================================

void ahci_irq_handler(void) {
    u32 global_is = hba->is;

    for (int i = 0; i < 32; i++) {
        if (!(global_is & (1u << i))) continue;

        struct hba_port *port = &hba->ports[i];
        u32 port_is = port->is;
        port->is = port_is;        // clear port interrupt status
        hba->is  = (1u << i);     // clear global interrupt status

        i32 status = (port_is & HBA_PORT_IS_TFES) ? -1 : 0;

        if (port_blk[i])
            blk_complete(port_blk[i], status);
    }
}

// ============================================================================
// Controller Initialization
// ============================================================================

void ahci_init(void) {
    // Find AHCI controller
    ahci_dev = 0;
    for (u32 i = 0; i < pci_device_count; i++) {
        struct pci_device *dev = &pci_devices[i];
        if (dev->hdr.general.h.class_code == PCI_CLASS_STORAGE && dev->hdr.general.h.subclass == PCI_SUBCLASS_AHCI) {
            ahci_dev = dev;
            break;
        }
    }

    if (!ahci_dev) {
        klog("AHCI", "no controller found");
        return;
    }

    klog("AHCI", "controller at %u:%u.%u", ahci_dev->bus, ahci_dev->slot, ahci_dev->func);

    u64 abar_phys = pci_read_bar(ahci_dev->bus, ahci_dev->slot, ahci_dev->func, 5);
    map_mmio(abar_phys, PAGE_SIZE * 4);
    hba = (struct hba_mem *)PHYS_TO_VIRT(abar_phys);
    hba->ghc |= HBA_GHC_AE;

    klog("AHCI", "version %x.%x, %u ports",
           (hba->vs >> 16) & 0xFFFF, hba->vs & 0xFFFF,
           HBA_CAP_NP(hba->cap) + 1);

    pci_msi_enable(ahci_dev, 48);

    u32 pi = hba->pi;
    for (int i = 0; i < 32; i++) {
        if (!(pi & (1 << i))) continue;

        struct hba_port *port = &hba->ports[i];
        int type = ahci_port_type(port);
        if (type == AHCI_DEV_NULL) continue;

        if (type == AHCI_DEV_SATA) {
            if (ahci_port_init(port) < 0) {
                klog_fail("AHCI", "failed to init port %d", i);
                continue;
            }
            if (!ahci_sata_port) ahci_sata_port = port;
            port->ie = HBA_PORT_IE_DHRE | HBA_PORT_IE_TFEE;

            u8 *id = kalloc(1);
            if (id && ahci_identify(port, id) == 0) {
                int port_num = get_port_num(port);
                ahci_parse_identify(id, &port_info[port_num]);
                struct ahci_port_info *info = &port_info[port_num];
                if (port == ahci_sata_port) ahci_sata_info = *info;

                char name[BLK_NAME_LEN] = "ahci0";
                name[4] = '0' + i;
                struct blk_ops ops = { .submit = ahci_submit };
                port_blk[i] = blk_register(name, ops,
                                            port_info[port_num].sector_size, port);
                if (port_blk[i])
                    klog_ok("AHCI", "disk %s  %s  %u MB", name, info->model,
                            (u32)(info->sector_count * info->sector_size / (1024*1024)));
            }
            if (id) kfree(id, 1);
        }
    }

    hba->ghc |= HBA_GHC_IE;
    klog_ok("AHCI", "initialized");
}

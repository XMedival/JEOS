#pragma once
#include "types.h"

// FIS Types
#define FIS_TYPE_REG_H2D   0x27  // Register FIS - host to device
#define FIS_TYPE_REG_D2H   0x34  // Register FIS - device to host
#define FIS_TYPE_DMA_ACT   0x39  // DMA activate FIS - device to host
#define FIS_TYPE_DMA_SETUP 0x41  // DMA setup FIS - bidirectional
#define FIS_TYPE_DATA      0x46  // Data FIS - bidirectional
#define FIS_TYPE_BIST      0x58  // BIST activate FIS - bidirectional
#define FIS_TYPE_PIO_SETUP 0x5F  // PIO setup FIS - device to host
#define FIS_TYPE_DEV_BITS  0xA1  // Set device bits FIS - device to host

// FIS flags
#define FIS_H2D_CMD (1 << 7)  // Command (vs control)

// Register FIS - Host to Device
struct fis_reg_h2d {
    u8 fis_type;
    u8 flags;         // [7]=cmd, [3:0]=pm port
    u8 command;
    u8 feature_lo;

    u8 lba0, lba1, lba2;
    u8 device;

    u8 lba3, lba4, lba5;
    u8 feature_hi;

    u16 count;
    u8 icc;
    u8 control;

    u8 _reserved[4];
} __attribute__((packed));

// Register FIS - Device to Host
struct fis_reg_d2h {
    u8 fis_type;
    u8 flags;         // [6]=interrupt, [3:0]=pm port
    u8 status;
    u8 error;

    u8 lba0, lba1, lba2;
    u8 device;

    u8 lba3, lba4, lba5;
    u8 _reserved0;

    u16 count;
    u8 _reserved1[6];
} __attribute__((packed));

// Data FIS
struct fis_data {
    u8 fis_type;
    u8 flags;
    u8 _reserved[2];
    u32 data[];  // variable length
} __attribute__((packed));

// PIO Setup FIS - Device to Host
struct fis_pio_setup {
    u8 fis_type;
    u8 flags;         // [6]=interrupt, [5]=direction, [3:0]=pm port
    u8 status;
    u8 error;

    u8 lba0, lba1, lba2;
    u8 device;

    u8 lba3, lba4, lba5;
    u8 _reserved0;

    u16 count;
    u8 _reserved1;
    u8 e_status;      // new status

    u16 transfer_count;
    u8 _reserved2[2];
} __attribute__((packed));

// DMA Setup FIS
struct fis_dma_setup {
    u8 fis_type;
    u8 flags;
    u8 _reserved0[2];

    u64 dma_buffer_id;
    u32 _reserved1;
    u32 dma_buffer_offset;
    u32 transfer_count;
    u32 _reserved2;
} __attribute__((packed));

// ============================================================================
// Port Registers (0x100 + port * 0x80)
// ============================================================================

struct hba_port {
    u32 clb;          // 0x00 Command List Base Address (low)
    u32 clbu;         // 0x04 Command List Base Address (high)
    u32 fb;           // 0x08 FIS Base Address (low)
    u32 fbu;          // 0x0C FIS Base Address (high)
    u32 is;           // 0x10 Interrupt Status
    u32 ie;           // 0x14 Interrupt Enable
    u32 cmd;          // 0x18 Command and Status
    u32 _reserved0;
    u32 tfd;          // 0x20 Task File Data
    u32 sig;          // 0x24 Signature
    u32 ssts;         // 0x28 SATA Status
    u32 sctl;         // 0x2C SATA Control
    u32 serr;         // 0x30 SATA Error
    u32 sact;         // 0x34 SATA Active
    u32 ci;           // 0x38 Command Issue
    u32 sntf;         // 0x3C SATA Notification
    u32 fbs;          // 0x40 FIS-based Switching Control
    u32 _reserved1[11];
    u32 vendor[4];
} __attribute__((packed));

// Port CMD bits
#define HBA_PORT_CMD_ST    (1 << 0)   // Start
#define HBA_PORT_CMD_SUD   (1 << 1)   // Spin-Up Device
#define HBA_PORT_CMD_POD   (1 << 2)   // Power On Device
#define HBA_PORT_CMD_FRE   (1 << 4)   // FIS Receive Enable
#define HBA_PORT_CMD_FR    (1 << 14)  // FIS Receive Running
#define HBA_PORT_CMD_CR    (1 << 15)  // Command List Running

// Port TFD bits
#define HBA_PORT_TFD_ERR   (1 << 0)   // Error
#define HBA_PORT_TFD_DRQ   (1 << 3)   // Data Request
#define HBA_PORT_TFD_BSY   (1 << 7)   // Busy

// Port SSTS (SATA Status)
#define HBA_PORT_SSTS_DET(x)  ((x) & 0xF)        // Device Detection
#define HBA_PORT_SSTS_IPM(x)  (((x) >> 8) & 0xF) // Interface Power Management
#define HBA_PORT_SSTS_DET_PRESENT  0x3           // Device present & PHY
#define HBA_PORT_SSTS_IPM_ACTIVE   0x1           // Interface active

// Port IE (Interrupt Enable) bits
#define HBA_PORT_IE_DHRE   (1 << 0)   // D2H Register FIS interrupt
#define HBA_PORT_IE_TFEE   (1 << 30)  // Task File Error Enable

#define HBA_PORT_IS_TFES       (1 << 30)         // Task File Error Status

// Port SCTL bits
#define SCTL_DET_MASK          0xF               // Device Detection Init mask
#define SCTL_DET_COMRESET      0x1               // Perform COMRESET

// Port signatures
#define SATA_SIG_ATA    0x00000101  // SATA drive
#define SATA_SIG_ATAPI  0xEB140101  // SATAPI drive
#define SATA_SIG_SEMB   0xC33C0101  // Enclosure management bridge
#define SATA_SIG_PM     0x96690101  // Port multiplier

// ============================================================================
// HBA Memory Registers (at BAR5)
// ============================================================================

// Generic Host Control registers (0x00 - 0x2B)
struct hba_mem {
    u32 cap;          // 0x00 Host Capabilities
    u32 ghc;          // 0x04 Global Host Control
    u32 is;           // 0x08 Interrupt Status
    u32 pi;           // 0x0C Ports Implemented
    u32 vs;           // 0x10 Version
    u32 ccc_ctl;      // 0x14 Command Completion Coalescing Control
    u32 ccc_ports;    // 0x18 Command Completion Coalescing Ports
    u32 em_loc;       // 0x1C Enclosure Management Location
    u32 em_ctl;       // 0x20 Enclosure Management Control
    u32 cap2;         // 0x24 Host Capabilities Extended
    u32 bohc;         // 0x28 BIOS/OS Handoff Control
    u8  _reserved[0xA0 - 0x2C];
    u8  vendor[0x100 - 0xA0];
    struct hba_port ports[];  // 0x100+ Port registers
} __attribute__((packed));

// GHC bits
#define HBA_GHC_AE     (1 << 31)  // AHCI Enable
#define HBA_GHC_IE     (1 << 1)   // Interrupt Enable
#define HBA_GHC_HR     (1 << 0)   // HBA Reset

// CAP bits
#define HBA_CAP_S64A   (1 << 31)  // 64-bit Addressing
#define HBA_CAP_NCQ    (1 << 30)  // Native Command Queuing
#define HBA_CAP_NP(cap) ((cap) & 0x1F)  // Number of ports - 1

// ============================================================================
// Command List & Command Table
// ============================================================================

// Command Header (32 bytes, 32 per port in Command List)
struct hba_cmd_header {
    u8  cfl : 5;      // Command FIS Length (in DWORDs)
    u8  a : 1;        // ATAPI
    u8  w : 1;        // Write
    u8  p : 1;        // Prefetchable

    u8  r : 1;        // Reset
    u8  b : 1;        // BIST
    u8  c : 1;        // Clear Busy upon R_OK
    u8  _reserved0 : 1;
    u8  pmp : 4;      // Port Multiplier Port

    u16 prdtl;        // PRDT Length (entries)

    volatile u32 prdbc;  // PRD Byte Count (transferred)

    u32 ctba;         // Command Table Base Address (low)
    u32 ctbau;        // Command Table Base Address (high)

    u32 _reserved1[4];
} __attribute__((packed));

// PRDT Entry (16 bytes)
struct hba_prdt_entry {
    u32 dba;          // Data Base Address (low)
    u32 dbau;         // Data Base Address (high)
    u32 _reserved;
    u32 dbc : 22;     // Data Byte Count (0 = 1 byte, max 4MB)
    u32 _reserved2 : 9;
    u32 i : 1;        // Interrupt on Completion
} __attribute__((packed));

// Command Table (variable size, 128 byte aligned)
struct hba_cmd_table {
    u8 cfis[64];                   // Command FIS
    u8 acmd[16];                   // ATAPI Command
    u8 _reserved[48];
    struct hba_prdt_entry prdt[];  // PRDT entries (up to 65535)
} __attribute__((packed));

// ============================================================================
// Received FIS (256 bytes per port)
// ============================================================================

struct hba_received_fis {
    struct fis_dma_setup dsfis;    // 0x00 DMA Setup FIS
    u8 _pad0[4];

    struct fis_pio_setup psfis;    // 0x20 PIO Setup FIS
    u8 _pad1[12];

    struct fis_reg_d2h rfis;       // 0x40 D2H Register FIS
    u8 _pad2[4];

    u8 sdbfis[8];                  // 0x58 Set Device Bits FIS

    u8 ufis[64];                   // 0x60 Unknown FIS

    u8 _reserved[0x100 - 0xA0];
} __attribute__((packed));

// ============================================================================
// ATA Commands
// ============================================================================

#define ATA_CMD_READ_DMA_EXT   0x25
#define ATA_CMD_WRITE_DMA_EXT  0x35
#define ATA_CMD_IDENTIFY       0xEC

#define ATA_DEV_LBA            (1 << 6)
#define ATA_SECTOR_SIZE_DEFAULT  512

// IDENTIFY data offsets (word indices)
#define ATA_ID_MODEL           27   // Words 27-46: Model string
#define ATA_ID_LBA48_SECTORS   100  // Words 100-103: LBA48 sector count
#define ATA_ID_SECTOR_SIZE     106  // Word 106: Physical/Logical sector size
#define ATA_ID_LOGICAL_SIZE    117  // Words 117-118: Logical sector size (if word 106 bit 12 set)

// IDENTIFY data offsets (in bytes)
#define ATA_IDENTIFY_MODEL_OFFSET   (ATA_ID_MODEL * 2)        // Bytes 54-93
#define ATA_IDENTIFY_LBA48_OFFSET   (ATA_ID_LBA48_SECTORS * 2) // Bytes 200-207

// Word 106 bits (Physical/Logical Sector Size)
#define ATA_ID_W106_VALID          (1 << 14)  // Word 106 is valid
#define ATA_ID_W106_LOGICAL_GT512  (1 << 12)  // Logical sector > 512 bytes
#define ATA_ID_W106_MULTI_LOGICAL  (1 << 13)  // Multiple logical per physical

// ============================================================================
// AHCI Driver Functions
// ============================================================================

// Initialization
void ahci_init(void);                              // Find controller, init all ports
int  ahci_port_init(struct hba_port *port);        // Init single port structures

// Port control
void ahci_port_start(struct hba_port *port);       // Start command engine
void ahci_port_stop(struct hba_port *port);        // Stop command engine
void ahci_port_reset(struct hba_port *port);       // COMRESET

// Device detection
int  ahci_port_type(struct hba_port *port);        // Check device type (ATA/ATAPI/none)
int  ahci_identify(struct hba_port *port, void *buf);  // IDENTIFY, fills 512-byte buf

// Low-level
int  ahci_find_slot(struct hba_port *port);        // Find free command slot (-1 if none)
int  ahci_issue_poll(struct hba_port *port, int slot); // Issue cmd, poll for completion
void ahci_submit_dma(struct hba_port *port, int slot); // Issue cmd, non-blocking

// IRQ handler (called from IDT vector 48)
void ahci_irq_handler(void);

// Helper: Set LBA48 address in FIS
static inline void fis_set_lba48(struct fis_reg_h2d *fis, u64 lba) {
    fis->lba0 = (u8)(lba);
    fis->lba1 = (u8)(lba >> 8);
    fis->lba2 = (u8)(lba >> 16);
    fis->lba3 = (u8)(lba >> 24);
    fis->lba4 = (u8)(lba >> 32);
    fis->lba5 = (u8)(lba >> 40);
}

// Helper: Clear port interrupt status
static inline void ahci_port_clear_interrupts(struct hba_port *port) {
    port->is = (u32)-1;
}

// Device types returned by ahci_port_type()
#define AHCI_DEV_NULL   0
#define AHCI_DEV_SATA   1
#define AHCI_DEV_SATAPI 2
#define AHCI_DEV_SEMB   3
#define AHCI_DEV_PM     4

// Per-port device information
struct ahci_port_info {
    u32 sector_size;      // Logical sector size in bytes
    u64 sector_count;     // Total sectors (LBA48)
    char model[41];       // Model string (null-terminated)
};

// First SATA port found (set by ahci_init)
extern struct hba_port *ahci_sata_port;
extern struct ahci_port_info ahci_sata_info;  // Info for first SATA port

// Get sector size for a port (returns 0 if not initialized)
u32 ahci_get_sector_size(struct hba_port *port);

// Parse IDENTIFY data and fill port info
void ahci_parse_identify(u8 *id, struct ahci_port_info *info);

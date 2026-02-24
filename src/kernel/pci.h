#pragma once
#include "types.h"
#include "x86.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define PCI_ADDR(bus, slot, func, off)                                         \
    (0x80000000 | ((u32)(bus) << 16) | ((u32)(slot) << 11) |                   \
     ((u32)(func) << 8) | ((off) & 0xFC))

#define PCI_CLASS_STORAGE 0x01
#define PCI_CLASS_NETWORK 0x02
#define PCI_CLASS_DISPLAY 0x03
#define PCI_CLASS_BRIDGE 0x06

#define MAX_PCI_DEVICES 256

// PCI config space offsets
#define PCI_BAR0_OFFSET    0x10
#define PCI_CAP_PTR_OFFSET 0x34
#define PCI_STATUS_OFFSET  0x06

// BAR type bits
#define PCI_BAR_TYPE_MASK   0x6
#define PCI_BAR_TYPE_64BIT  0x4
#define PCI_BAR_ADDR_MASK   (~0xFULL)

// MSI address base (x86)
#define MSI_ADDR_BASE      0xFEE00000

// ============================================================================
// PCI Status Register bits (offset 0x06)
// ============================================================================
#define PCI_STATUS_CAP_LIST    (1 << 4)   // Has capabilities list

// ============================================================================
// PCI Capability IDs
// ============================================================================
#define PCI_CAP_PM             0x01  // Power Management
#define PCI_CAP_MSI            0x05  // Message Signaled Interrupts
#define PCI_CAP_PCIE           0x10  // PCI Express
#define PCI_CAP_MSIX           0x11  // MSI-X

// ============================================================================
// MSI Capability (ID = 0x05)
// ============================================================================
struct pci_cap_msi {
    u8  cap_id;           // 0x05
    u8  next_ptr;
    u16 msg_ctrl;         // Message Control
    u32 msg_addr;         // Message Address (low)
    u32 msg_addr_hi;      // Message Address (high) - only if 64-bit capable
    u16 msg_data;         // Message Data
    // Optional: per-vector masking if supported
} __attribute__((packed));

// MSI Message Control bits
#define MSI_CTRL_ENABLE        (1 << 0)
#define MSI_CTRL_64BIT         (1 << 7)   // 64-bit address capable
#define MSI_CTRL_PERVEC_MASK   (1 << 8)   // Per-vector masking capable
#define MSI_CTRL_MMC_MASK      (0x7 << 1) // Multiple Message Capable (log2)
#define MSI_CTRL_MME_MASK      (0x7 << 4) // Multiple Message Enable (log2)

// ============================================================================
// MSI-X Capability (ID = 0x11)
// ============================================================================
struct pci_cap_msix {
    u8  cap_id;           // 0x11
    u8  next_ptr;
    u16 msg_ctrl;         // Message Control
    u32 table_offset;     // Table Offset & BIR
    u32 pba_offset;       // PBA Offset & BIR
} __attribute__((packed));

// MSI-X Message Control bits
#define MSIX_CTRL_ENABLE       (1 << 15)
#define MSIX_CTRL_FUNC_MASK    (1 << 14)  // Function Mask
#define MSIX_CTRL_TABLE_SIZE   0x7FF      // Table Size (entries - 1)

// MSI-X Table Entry (in memory, pointed to by BAR)
struct msix_entry {
    u32 addr_lo;
    u32 addr_hi;
    u32 data;
    u32 ctrl;             // Bit 0 = masked
} __attribute__((packed));

struct pci_common_header {
    u16 vendor_id;
    u16 device_id;
    u16 command;
    u16 status;
    u8 revision;
    u8 prog_if;
    u8 subclass;
    u8 class_code;
    u8 cache_line_size;
    u8 latency_timer;
    u8 header_type;
    u8 bist;
} __attribute__((packed));

struct pci_general_header {
    struct pci_common_header h;
    u32 bar0;
    u32 bar1;
    u32 bar2;
    u32 bar3;
    u32 bar4;
    u32 bar5;
    u32 cis_ptr;
    u16 sub_vendor_id;
    u16 subsystem_id;
    u32 expansion_addr;
    u8 capabilities_ptr;
    u8 _[3];
    u32 __;
    u8 int_line;
    u8 int_pin;
    u8 min_grant;
    u8 max_latency;
} __attribute__((packed));

struct pci_bridge_header {
    struct pci_common_header h;
    u32 bar0;
    u32 bar1;
    u8 primary_bus_num;
    u8 secondary_bus_num;
    u8 subordinate_bus_num;
    u8 secondary_latency_timer;
    u8 io_base;
    u8 io_limit;
    u16 secondary_status;
    u16 memory_base;
    u16 memory_limit;
    u16 prefetchable_mem_base;
    u16 prefetchable_mem_limit;
    u32 prefetchable_mem_base32;
    u32 prefetchable_mem_limit32;
    u16 io_base_upper;
    u16 io_limit_upper;
    u8 capabilities_ptr;
    u8 _[3];
    u32 expansion_addr;
    u8 int_line;
    u8 int_pin;
    u16 bridge_control;
} __attribute__((packed));

struct pci_device {
    u8 bus, slot, func;
    u16 vendor_id, device_id;
    u8 header_type;
    u8 int_line;
    union {
        struct pci_general_header general;
        struct pci_bridge_header  bridge;
    } hdr;
};

extern struct pci_device pci_devices[MAX_PCI_DEVICES];
extern u32 pci_device_count;

// ============================================================================
// PCI Functions
// ============================================================================

// Read full config space headers into structs
void pci_read_general_header(u8 bus, u8 slot, u8 func, struct pci_general_header *out);
void pci_read_bridge_header(u8 bus, u8 slot, u8 func, struct pci_bridge_header *out);

// Basic config space access
u8   pci_read8(u8 bus, u8 slot, u8 func, u8 offset);
u16  pci_read16(u8 bus, u8 slot, u8 func, u8 offset);
u32  pci_read32(u8 bus, u8 slot, u8 func, u8 offset);
void pci_write8(u8 bus, u8 slot, u8 func, u8 offset, u8 val);
void pci_write16(u8 bus, u8 slot, u8 func, u8 offset, u16 val);
void pci_write32(u8 bus, u8 slot, u8 func, u8 offset, u32 val);

// Device-bound config space access (convenience wrappers)
static inline u32 pci_dev_read32(struct pci_device *dev, u8 off) {
    return pci_read32(dev->bus, dev->slot, dev->func, off);
}

static inline void pci_dev_write32(struct pci_device *dev, u8 off, u32 val) {
    pci_write32(dev->bus, dev->slot, dev->func, off, val);
}

static inline u16 pci_dev_read16(struct pci_device *dev, u8 off) {
    return pci_read16(dev->bus, dev->slot, dev->func, off);
}

static inline void pci_dev_write16(struct pci_device *dev, u8 off, u16 val) {
    pci_write16(dev->bus, dev->slot, dev->func, off, val);
}

static inline u8 pci_dev_read8(struct pci_device *dev, u8 off) {
    return pci_read8(dev->bus, dev->slot, dev->func, off);
}

static inline void pci_dev_write8(struct pci_device *dev, u8 off, u8 val) {
    pci_write8(dev->bus, dev->slot, dev->func, off, val);
}

// Higher-level helpers
u64  pci_read_bar(u8 bus, u8 slot, u8 func, u8 bar);
void pci_scan_bus(u8 bus);
void pci_scan(void);

// Capability list
u8   pci_find_cap(u8 bus, u8 slot, u8 func, u8 cap_id);  // Returns offset or 0

// MSI helpers
int  pci_msi_enable(struct pci_device *dev, u8 vector);
void pci_msi_disable(struct pci_device *dev);

// PCI subclass for storage controllers
#define PCI_SUBCLASS_AHCI  0x06  // SATA AHCI
#define PCI_SUBCLASS_IDE   0x01  // IDE
#define PCI_SUBCLASS_NVME  0x08  // NVMe

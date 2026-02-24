#include "pci.h"
#include "print.h"

struct pci_device pci_devices[MAX_PCI_DEVICES];
u32 pci_device_count = 0;

u8 pci_read8(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 address = PCI_ADDR(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDR, address);
    return (inl(PCI_CONFIG_DATA) >> ((offset & 3) * 8)) & 0xFF;
}

u16 pci_read16(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 address = PCI_ADDR(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDR, address);
    return (inl(PCI_CONFIG_DATA) >> ((offset & 2) * 8)) & 0xFFFF;
}

u32 pci_read32(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 address = PCI_ADDR(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDR, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write8(u8 bus, u8 slot, u8 func, u8 offset, u8 val) {
    u32 address = PCI_ADDR(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDR, address);
    u32 data = inl(PCI_CONFIG_DATA);
    int shift = (offset & 3) * 8;
    data &= ~(0xFF << shift);
    data |= (u32)val << shift;
    outl(PCI_CONFIG_DATA, data);
}

void pci_write16(u8 bus, u8 slot, u8 func, u8 offset, u16 val) {
    u32 address = PCI_ADDR(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDR, address);
    u32 data = inl(PCI_CONFIG_DATA);
    int shift = (offset & 2) * 8;
    data &= ~(0xFFFF << shift);
    data |= (u32)val << shift;
    outl(PCI_CONFIG_DATA, data);
}

void pci_write32(u8 bus, u8 slot, u8 func, u8 offset, u32 val) {
    u32 address = PCI_ADDR(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDR, address);
    outl(PCI_CONFIG_DATA, val);
}

// Read a BAR, handling 64-bit BARs (returns physical address)
u64 pci_read_bar(u8 bus, u8 slot, u8 func, u8 bar) {
    u8 offset = PCI_BAR0_OFFSET + bar * 4;
    u32 low = pci_read32(bus, slot, func, offset);

    // Check BAR type (bits 2:1)
    // 00 = 32-bit, 10 = 64-bit
    if ((low & PCI_BAR_TYPE_MASK) == PCI_BAR_TYPE_64BIT) {
        // 64-bit BAR: read high 32 bits from next BAR
        u32 high = pci_read32(bus, slot, func, offset + 4);
        return ((u64)high << 32) | (low & PCI_BAR_ADDR_MASK);
    }
    return low & PCI_BAR_ADDR_MASK;  // Mask off type bits
}

// Find capability in capability list, returns offset or 0 if not found
u8 pci_find_cap(u8 bus, u8 slot, u8 func, u8 cap_id) {
    // Check if device has capabilities
    u16 status = pci_read16(bus, slot, func, PCI_STATUS_OFFSET);
    if (!(status & PCI_STATUS_CAP_LIST))
        return 0;

    // Get capabilities pointer
    u8 ptr = pci_read8(bus, slot, func, PCI_CAP_PTR_OFFSET) & ~0x3;

    // Walk the list
    while (ptr) {
        u8 id = pci_read8(bus, slot, func, ptr);
        if (id == cap_id)
            return ptr;
        ptr = pci_read8(bus, slot, func, ptr + 1) & ~0x3;
    }

    return 0;
}

// Enable MSI for a device, returns 0 on success
int pci_msi_enable(struct pci_device *dev, u8 vector) {
    u8 cap = pci_find_cap(dev->bus, dev->slot, dev->func, PCI_CAP_MSI);
    if (!cap)
        return -1;

    // Read message control
    u16 ctrl = pci_read16(dev->bus, dev->slot, dev->func, cap + 2);

    // MSI address format for x86: MSI_ADDR_BASE | (dest_apic << 12)
    // We send to LAPIC 0, edge triggered, fixed delivery
    u32 addr = MSI_ADDR_BASE;  // Dest APIC 0

    // Write address
    pci_write32(dev->bus, dev->slot, dev->func, cap + 4, addr);

    // If 64-bit capable, write upper address and adjust data offset
    int data_off = cap + 8;
    if (ctrl & MSI_CTRL_64BIT) {
        pci_write32(dev->bus, dev->slot, dev->func, cap + 8, 0);
        data_off = cap + 12;
    }

    // Write data (vector number)
    pci_write16(dev->bus, dev->slot, dev->func, data_off, vector);

    // Enable MSI (1 message)
    ctrl &= ~MSI_CTRL_MME_MASK;  // Clear MME (use 1 vector)
    ctrl |= MSI_CTRL_ENABLE;
    pci_write16(dev->bus, dev->slot, dev->func, cap + 2, ctrl);

    return 0;
}

void pci_msi_disable(struct pci_device *dev) {
    u8 cap = pci_find_cap(dev->bus, dev->slot, dev->func, PCI_CAP_MSI);
    if (!cap)
        return;

    u16 ctrl = pci_read16(dev->bus, dev->slot, dev->func, cap + 2);
    ctrl &= ~MSI_CTRL_ENABLE;
    pci_write16(dev->bus, dev->slot, dev->func, cap + 2, ctrl);
}

void pci_read_general_header(u8 bus, u8 slot, u8 func,
                             struct pci_general_header *out) {
    u32 *p = (u32 *)out;
    for (u8 i = 0; i < sizeof(*out) / 4; i++)
        p[i] = pci_read32(bus, slot, func, i * 4);
}

void pci_read_bridge_header(u8 bus, u8 slot, u8 func,
                            struct pci_bridge_header *out) {
    u32 *p = (u32 *)out;
    for (u8 i = 0; i < sizeof(*out) / 4; i++)
        p[i] = pci_read32(bus, slot, func, i * 4);
}

u8 pci_bus_alloc(void) {
    static u8 free_bus = 1;
    return free_bus++;
}

static void pci_scan_function(u8 bus, u8 slot, u8 func) {
    if (pci_read16(bus, slot, func, 0) == 0xFFFF)
        return;

    if (pci_device_count >= MAX_PCI_DEVICES)
        return;

    struct pci_device *dev = &pci_devices[pci_device_count++];
    dev->bus  = bus;
    dev->slot = slot;
    dev->func = func;

    u8 htype = pci_read8(bus, slot, func, 0x0E);
    dev->header_type = htype;

    if ((htype & 0x7F) == 0x01) {
        // PCI-to-PCI bridge
        pci_read_bridge_header(bus, slot, func, &dev->hdr.bridge);
        dev->vendor_id  = dev->hdr.bridge.h.vendor_id;
        dev->device_id  = dev->hdr.bridge.h.device_id;
        dev->int_line   = dev->hdr.bridge.int_line;

        u8 secondary = pci_bus_alloc();
        dev->hdr.bridge.primary_bus_num      = bus;
        dev->hdr.bridge.secondary_bus_num    = secondary;
        dev->hdr.bridge.subordinate_bus_num  = secondary; // tightened after scan

        // Write bus numbers to bridge config space
        pci_write8(bus, slot, func, 0x18, bus);       // primary
        pci_write8(bus, slot, func, 0x19, secondary); // secondary
        pci_write8(bus, slot, func, 0x1A, secondary); // subordinate (updated after)

        klog("PCI", "bridge %u:%u.%u -> bus %u", bus, slot, func, secondary);

        pci_scan_bus(secondary);

        // Update subordinate to reflect highest bus allocated
        u8 sub = pci_bus_alloc() - 1; // peek at last allocated
        pci_write8(bus, slot, func, 0x1A, sub);
        dev->hdr.bridge.subordinate_bus_num = sub;
    } else {
        // Endpoint (type 0x00) or other
        pci_read_general_header(bus, slot, func, &dev->hdr.general);
        dev->vendor_id  = dev->hdr.general.h.vendor_id;
        dev->device_id  = dev->hdr.general.h.device_id;
        dev->int_line   = dev->hdr.general.int_line;

        klog("PCI", "%u:%u.%u  vendor=%04X device=%04X class=%02x:%02x",
               bus, slot, func, dev->vendor_id, dev->device_id,
               dev->hdr.general.h.class_code, dev->hdr.general.h.subclass);
    }
}

void pci_scan_bus(u8 bus) {
    for (u8 slot = 0; slot < 32; slot++) {
        if (pci_read16(bus, slot, 0, 0) == 0xFFFF)
            continue;

        pci_scan_function(bus, slot, 0);

        // Check multi-function bit in header_type
        u8 htype = pci_read8(bus, slot, 0, 0x0E);
        if (htype & 0x80) {
            for (u8 func = 1; func < 8; func++)
                pci_scan_function(bus, slot, func);
        }
    }
}

void pci_scan(void) {
    pci_scan_bus(0);
}

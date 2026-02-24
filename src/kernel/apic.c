#include "apic.h"
#include "acpi.h"
#include "mem.h"
#include "x86.h"
#include "print.h"
#include "pit.h"

static volatile u32 *lapic_base;
static volatile u32 *ioapic_base;

// Local APIC read/write
static inline u32 lapic_read(u32 reg) {
    return lapic_base[reg / 4];
}

static inline void lapic_write(u32 reg, u32 val) {
    lapic_base[reg / 4] = val;
}

// I/O APIC read/write
static inline u32 ioapic_read(u32 reg) {
    ioapic_base[IOAPIC_REGSEL / 4] = reg;
    return ioapic_base[IOAPIC_REGWIN / 4];
}

static inline void ioapic_write(u32 reg, u32 val) {
    ioapic_base[IOAPIC_REGSEL / 4] = reg;
    ioapic_base[IOAPIC_REGWIN / 4] = val;
}

void pic_disable(void) {
    // Mask all IRQs on both PICs
    outb(0xA1, 0xFF);  // Slave PIC
    outb(0x21, 0xFF);  // Master PIC
}

void lapic_init(void) {
    struct MADT *madt = acpi_tables.madt;
    if (!madt) {
        klog_fail("LAPIC", "no MADT found");
        return;
    }
    // (verbose removed)

    // Get LAPIC base from MADT
    lapic_base = PHYS_TO_VIRT((u64)madt->local_apic_addr);
    // (verbose removed)

    // Check for LAPIC address override in MADT entries
    u8 *ptr = (u8 *)madt + MADT_ENTRIES_OFFSET;
    u8 *end = (u8 *)madt + madt->h.length;

    while (ptr < end) {
        u8 type = ptr[0];
        u8 len = ptr[1];

        if (len == 0) break;  // prevent infinite loop

        if (type == MADT_LAPIC_OVERRIDE) {
            u64 addr = *(u64 *)(ptr + 4);
            lapic_base = PHYS_TO_VIRT(addr);
        }
        ptr += len;
    }
    // (verbose removed)

    // Map LAPIC MMIO region
    
    map_mmio((u64)madt->local_apic_addr, PAGE_SIZE);

    // Enable LAPIC via Spurious Vector Register
    
    lapic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | LAPIC_SPURIOUS_VECTOR);
    // (verbose removed)

    // Set task priority to 0 (accept all interrupts)
    lapic_write(LAPIC_TPR, 0);

    klog_ok("LAPIC", "initialized");
}

void lapic_eoi(void) {
    lapic_write(LAPIC_EOI, 0);
}

void lapic_init_ap(void) {
    // LAPIC MMIO is already mapped by BSP; just enable this CPU's LAPIC
    lapic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | LAPIC_SPURIOUS_VECTOR);
    lapic_write(LAPIC_TPR, 0);
}

u32 lapic_id(void) {
    return lapic_read(LAPIC_ID) >> 24;
}

void ioapic_init(void) {
    struct MADT *madt = acpi_tables.madt;
    if (!madt) {
        klog_fail("IOAPIC", "no MADT found");
        return;
    }

    // Parse MADT entries to find I/O APIC
    u8 *ptr = (u8 *)madt + MADT_ENTRIES_OFFSET;
    u8 *end = (u8 *)madt + madt->h.length;

    while (ptr < end) {
        u8 type = ptr[0];
        u8 len = ptr[1];

        if (type == MADT_IOAPIC) {
            u32 addr = ptr[4] | (ptr[5] << 8) | (ptr[6] << 16) | (ptr[7] << 24);
            map_mmio((u64)addr, PAGE_SIZE);
            ioapic_base = PHYS_TO_VIRT((u64)addr);
            break;
        }
        ptr += len;
    }

    if (!ioapic_base) {
        klog_fail("IOAPIC", "not found in MADT");
        return;
    }

    // Mask all IRQs initially
    u32 max_redir = IOAPIC_VER_MAX_REDIR(ioapic_read(IOAPIC_VER));
    for (u32 i = 0; i <= max_redir; i++) {
        ioapic_write(IOAPIC_REDTBL_LO(i), IOAPIC_REDTBL_MASKED);
        ioapic_write(IOAPIC_REDTBL_HI(i), 0);
    }

    klog_ok("IOAPIC", "initialized");
}

// Translate ISA IRQ to GSI (checks for Interrupt Source Overrides)
static u32 irq_to_gsi(u8 irq) {
    struct MADT *madt = acpi_tables.madt;
    if (!madt) return irq;

    u8 *ptr = (u8 *)madt + MADT_ENTRIES_OFFSET;
    u8 *end = (u8 *)madt + madt->h.length;

    while (ptr < end) {
        u8 type = ptr[0];
        u8 len = ptr[1];
        if (len == 0) break;

        // Type 2 = Interrupt Source Override
        if (type == MADT_ISO) {
            u8 source_irq = ptr[3];
            u32 gsi = *(u32 *)(ptr + 4);
            if (source_irq == irq)
                return gsi;
        }
        ptr += len;
    }
    return irq;  // no override, GSI == IRQ
}

void ioapic_route_irq(u8 irq, u8 vector, u8 dest_lapic_id) {
    u32 gsi = irq_to_gsi(irq);
    u32 lo = vector;
    u32 hi = (u32)dest_lapic_id << 24;
    ioapic_write(IOAPIC_REDTBL_HI(gsi), hi);
    ioapic_write(IOAPIC_REDTBL_LO(gsi), lo);
}

void ioapic_mask_irq(u8 irq) {
    u32 gsi = irq_to_gsi(irq);
    u32 lo = ioapic_read(IOAPIC_REDTBL_LO(gsi));
    lo |= IOAPIC_REDTBL_MASKED;
    ioapic_write(IOAPIC_REDTBL_LO(gsi), lo);
}

void ioapic_unmask_irq(u8 irq) {
    u32 gsi = irq_to_gsi(irq);
    u32 lo = ioapic_read(IOAPIC_REDTBL_LO(gsi));
    lo &= ~IOAPIC_REDTBL_MASKED;
    ioapic_write(IOAPIC_REDTBL_LO(gsi), lo);
}

// PIT (Programmable Interval Timer)
void pit_init(u32 hz) {
    u16 divisor = PIT_FREQ / hz;
    outb(PIT_CMD, PIT_CMD_CH0_SQUARE);
    outb(PIT_CH0, divisor & 0xFF);
    outb(PIT_CH0, (divisor >> 8) & 0xFF);
}

void pit_stop(void) {
    outb(PIT_CMD, PIT_CMD_CH0_ONESHOT);
    outb(PIT_CH0, 0);
    outb(PIT_CH0, 0);
}

// LAPIC Timer
#define LAPIC_TIMER_PERIODIC 0x20000
#define LAPIC_TIMER_MASKED   0x10000

void lapic_timer_init(u8 vector, u32 initial_count) {
    lapic_write(LAPIC_TIMER_DIV, LAPIC_TIMER_DIV_16);
    lapic_write(LAPIC_TIMER, vector);        // one-shot mode
    lapic_write(LAPIC_TIMER_INIT, initial_count);
}

void lapic_timer_periodic(u8 vector, u32 initial_count) {
    lapic_write(LAPIC_TIMER_DIV, LAPIC_TIMER_DIV_16);
    lapic_write(LAPIC_TIMER, vector | LAPIC_TIMER_PERIODIC);
    lapic_write(LAPIC_TIMER_INIT, initial_count);
}

void lapic_timer_stop(void) {
    lapic_write(LAPIC_TIMER, LAPIC_TIMER_MASKED);
    lapic_write(LAPIC_TIMER_INIT, 0);
}

#include "acpi.h"
#include "mem.h"

struct acpi_tables acpi_tables;

static u8 sig_eq(char sig[4], const char *expect) {
    return (sig[0] == expect[0] && sig[1] == expect[1] &&
            sig[2] == expect[2] && sig[3] == expect[3]);
}

void init_acpi(struct RSDP *rsdp) {
    acpi_tables.rsdp = rsdp;

    if (rsdp->Revision >= 2) {
        struct XSDP *xsdp = (struct XSDP *)rsdp;
        struct XSDT *xsdt = PHYS_TO_VIRT((u64)xsdp->xsdt);
        acpi_tables.xsdp = xsdp;
        acpi_tables.xsdt = xsdt;

        if (xsdp->xsdt != 0) {
            u64 num_entries = (xsdt->h.length - sizeof(xsdt->h)) / 8;
            for (u64 i = 0; i < num_entries; i++) {
                struct ACPISDTHeader *entry = PHYS_TO_VIRT(xsdt->SDTptrs[i]);
                if (sig_eq(entry->signature, "APIC"))
                    acpi_tables.madt = (struct MADT *)entry;
            }
        }
    } else {
        struct RSDT *rsdt = PHYS_TO_VIRT((u64)rsdp->rsdt);
        acpi_tables.rsdt = rsdt;

        u32 num_entries = (rsdt->h.length - sizeof(rsdt->h)) / 4;
        for (u32 i = 0; i < num_entries; i++) {
            struct ACPISDTHeader *entry = PHYS_TO_VIRT((u64)rsdt->SDTptrs[i]);
            if (sig_eq(entry->signature, "APIC"))
                acpi_tables.madt = (struct MADT *)entry;
        }
    }
}

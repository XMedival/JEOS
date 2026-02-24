#pragma once
#include "types.h"

struct acpi_tables {
    struct RSDP *rsdp;
    struct XSDP *xsdp;
    struct RSDT *rsdt;
    struct XSDT *xsdt;
    struct MADT *madt;
};

extern struct acpi_tables acpi_tables;

struct ACPISDTHeader {
    char signature[4];
    u32 length;
    u8 revision;
    u8 checksum;
    char OEMID[6];
    char OEMtableID[8];
    u32 OEMrevision;
    u32 creatorID;
    u32 creator_revision;
};

struct RSDP {
 char Signature[8];
 uint8_t Checksum;
 char OEMID[6];
 uint8_t Revision;
 struct RSDT *rsdt;
} __attribute__ ((packed));

struct XSDP {
 char Signature[8];
 uint8_t Checksum;
 char OEMID[6];
 uint8_t Revision;
 struct RSDT *rsdt;      // deprecated since version 2.0

 uint32_t Length;
 struct XSDT *xsdt;
 uint8_t ExtendedChecksum;
 uint8_t reserved[3];
} __attribute__((packed));

struct RSDT {
    struct ACPISDTHeader h;
    u32 SDTptrs[];
};

// MADT entry header (common to all entry types)
struct madt_entry_header {
    u8 type;
    u8 length;
} __attribute__((packed));

// MADT entry type 0: Local APIC
struct madt_entry_lapic {
    struct madt_entry_header h;
    u8 cpu_id;
    u8 apic_id;
    u32 flags;
} __attribute__((packed));

// MADT entry type 1: I/O APIC
struct madt_entry_ioapic {
    struct madt_entry_header h;
    u8 id;
    u8 _reserved0;
    u32 addr;
    u32 gsi_base;
} __attribute__((packed));

// MADT entry type 2: Interrupt Source Override
struct madt_entry_iso {
    struct madt_entry_header h;
    u8 bus;
    u8 source;
    u32 gsi;
    u16 flags;
} __attribute__((packed));

// MADT entry type 4: NMI Source
struct madt_entry_nmi {
    struct madt_entry_header h;
    u8 nmi_source;
    u8 _reserved0;
    u16 flags;
    u8 lint;
} __attribute__((packed));

// MADT entry type 5: Local APIC Address Override
struct madt_entry_lapic_override {
    struct madt_entry_header h;
    u16 _reserved0;
    u64 lapic_addr;
} __attribute__((packed));

struct MADT {
    struct ACPISDTHeader h;
    u32 local_apic_addr;
    u32 flags;
    // Variable-length entries follow; use MADT_ENTRIES_OFFSET to access
    u8 entries[];
};

struct GenericAddressStructure {
    u8 AddressSpace;
    u8 BitWidth;
    u8 BitOffset;
    u8 AccessSize;
    u64 Address;
};

struct FADT {
    struct ACPISDTHeader h;
    u32 firmwareCtrl;
    u32 DSDT;
    u8 _;
    u8 preffered_power_management_profile;
    u16 sci_int;
    u32 smi_cmd_port;
    u8 acpiEnable;
    u8 acpiDisable;
    u8 S4BIOS_REQ;
    u8 PSTATE_Ctrl;
    u32 PM1aEventBlock;
    u32 PM1bEventBlock;
    u32 PM1aControlBlock;
    u32 PM1bControlBlock;
    u32 PM2ControlBlock;
    u32 PMTimerBlock;
    u32 GPE0Block;
    u32 GPE1Block;
    u8 PM1EventLength;
    u8 PM1ControlLenght;
    u8 PM2ControlLenght;
    u8 PMTimerLength;
    u8 GPE0Length;
    u8 GPE1Length;
    u8 GPE1Base;
    u8 CStateControl;
    u16 WorstC2Latency;
    u16 WorstC3Latency;
    u16 FlushSize;
    u16 FlushStride;
    u8 DutyOffset;
    u8 DutyWidth;
    u8 DayAlarm;
    u8 MonthAlarm;
    u8 Century;
    u16 BootArchFlags;
    u8 __;
    u32 flags;
    struct GenericAddressStructure ResetReg;
    u8 ResetValue;
    u8 ___[3];
    u64 X_FirmwareControl;
    u64 X_DSDT;
    struct GenericAddressStructure X_PM1aEventBlock;
    struct GenericAddressStructure X_PM1bEventBlock;
    struct GenericAddressStructure X_PM1aControlBlock;
    struct GenericAddressStructure X_PM1bControlBlock;
    struct GenericAddressStructure X_PM2ControlBlock;
    struct GenericAddressStructure X_PMTimerBlock;
    struct GenericAddressStructure X_GPE0Block;
    struct GenericAddressStructure X_GPE1Block;
};

struct XSDT {
    struct ACPISDTHeader h;
    u64 SDTptrs[];
};

void init_acpi(struct RSDP *rsdp);

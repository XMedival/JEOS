#include "gdt.h"
#include "mem.h"
#include "spinlock.h"

struct gdt_entry {
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  access;
    u8  granularity;
    u8  base_high;
} __attribute__((packed));

struct tss_descriptor {
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  access;
    u8  granularity;
    u8  base_high;
    u32 base_upper;
    u32 reserved;
} __attribute__((packed));

struct gdt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

// Per-CPU GDT and TSS
static u8 gdt_data[MAX_CPUS][11 * 8] __attribute__((aligned(16)));
static struct tss tss_array[MAX_CPUS];

static void gdt_set_entry(u8 *gdt, int idx, u32 base, u32 limit, u8 access, u8 gran) {
    struct gdt_entry *e = (struct gdt_entry *)&gdt[idx * 8];
    e->limit_low   = limit & 0xFFFF;
    e->base_low    = base & 0xFFFF;
    e->base_mid    = (base >> 16) & 0xFF;
    e->access      = access;
    e->granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    e->base_high   = (base >> 24) & 0xFF;
}

static void gdt_set_tss(u8 *gdt, int idx, u64 base, u32 limit) {
    struct tss_descriptor *d = (struct tss_descriptor *)&gdt[idx * 8];
    d->limit_low   = limit & 0xFFFF;
    d->base_low    = base & 0xFFFF;
    d->base_mid    = (base >> 16) & 0xFF;
    d->access      = 0x89;  // present, 64-bit TSS (available)
    d->granularity = (limit >> 16) & 0x0F;
    d->base_high   = (base >> 24) & 0xFF;
    d->base_upper  = (base >> 32) & 0xFFFFFFFF;
    d->reserved    = 0;
}

static void setup_gdt(u8 *gdt, struct tss *tss) {
    memset(gdt, 0, 11 * 8);
    memset(tss, 0, sizeof(*tss));
    tss->iopb_offset = sizeof(*tss);

    gdt_set_entry(gdt, 0, 0, 0, 0, 0);
    gdt_set_entry(gdt, 1, 0, 0xFFFF, 0x9A, 0x00);
    gdt_set_entry(gdt, 2, 0, 0xFFFF, 0x92, 0x00);
    gdt_set_entry(gdt, 3, 0, 0xFFFFF, 0x9A, 0xCF);
    gdt_set_entry(gdt, 4, 0, 0xFFFFF, 0x92, 0xCF);
    gdt_set_entry(gdt, 5, 0, 0xFFFFF, 0x9A, 0xAF);
    gdt_set_entry(gdt, 6, 0, 0xFFFFF, 0x92, 0xAF);
    gdt_set_entry(gdt, 7, 0, 0xFFFFF, 0xF2, 0xAF);
    gdt_set_entry(gdt, 8, 0, 0xFFFFF, 0xFA, 0xAF);
    gdt_set_tss(gdt, 9, (u64)tss, sizeof(*tss) - 1);
}

static void load_gdt(u8 *gdt) {
    struct gdt_ptr gdtr;
    gdtr.limit = 11 * 8 - 1;
    gdtr.base = (u64)gdt;
    asm volatile("lgdt %0" : : "m"(gdtr));

    asm volatile(
        "pushq $0x28\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        ::: "rax", "memory"
    );

    asm volatile(
        "movw $0x30, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        "movw $0x00, %%ax\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        ::: "rax"
    );

    asm volatile("ltr %w0" : : "r"((u16)TSS_SEL));
}

void init_gdt(void) {
    setup_gdt(gdt_data[0], &tss_array[0]);
    load_gdt(gdt_data[0]);
}

void init_gdt_ap(u8 cpu_id) {
    setup_gdt(gdt_data[cpu_id], &tss_array[cpu_id]);
    load_gdt(gdt_data[cpu_id]);
}

void tss_set_rsp0(u64 rsp0) {
    tss_array[mycpu()->cpu_id].rsp0 = rsp0;
}

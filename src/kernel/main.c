#include "acpi.h"
#include "apic.h"
#include "ata.h"
#include "blk.h"
#include "kconsole.h"
#include "gdt.h"
#include "idt.h"
#include "initfs.h"
#include "limine.h"
#include "mem.h"
#include "panic.h"
#include "print.h"
#include "proc.h"
#include "ps2.h"
#include "serial.h"
#include "syscall.h"
#include "types.h"
#include "x86.h"
#include "pci.h"
#include "ahci.h"
#include "devfs.h"
#include "ext2.h"
#include "vfs.h"

/* Limine requests */

__attribute__((used, section(".requests_start_marker")))
static volatile uint64_t limine_requests_start[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(3);

__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request fb_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".requests")))
static volatile struct limine_mp_request smp_request = {
    .id = LIMINE_MP_REQUEST_ID,
    .revision = 0,
    .flags = LIMINE_MP_RESPONSE_X86_64_X2APIC,
};

__attribute__((used, section(".requests")))
static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".requests_end_marker")))
static volatile uint64_t limine_requests_end[] = LIMINE_REQUESTS_END_MARKER;

static volatile u64 ap_started = 0;

void ap_entry(struct limine_mp_info *info) {
    // Find our CPU slot
    // u32 apic_id = info->lapic_id;
    // struct cpu *c = 0;
    // for (u32 i = 0; i < ncpu; i++) {
    //     if (cpus[i].apic_id == apic_id) {
    //         c = &cpus[i];
    //         break;
    //     }
    // }
    // if (!c) { cli(); hlt(); }
    //
    // // Set GS base before and after GDT init (loading GS=0 clears it)
    // wrmsr(MSR_GS_BASE, (u64)c);
    // wrmsr(MSR_KERNEL_GS_BASE, (u64)c);
    //
    // init_gdt_ap(c->cpu_id);
    //
    // // Restore GS base after GDT reload
    // wrmsr(MSR_GS_BASE, (u64)c);
    // wrmsr(MSR_KERNEL_GS_BASE, (u64)c);
    // load_idt();
    // lapic_init_ap();
    // init_syscall();
    // lapic_timer_periodic(32, 1000000);
    //
    // __sync_fetch_and_add(&ap_started, 1);
    //
    // scheduler();
    for (;;) {
        cli();
        hlt();
    }
}

void _start(void) {
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        hlt();
    }
    init_idt();
    init_serial();
    kconsole_init(fb_request.response->framebuffers[0]);

    puts("\r\n\033[1;36m  ====  OS Kernel  ====\033[0m\r\n");

    /* BSP cpu slot must be set up before any spinlock/mycpu() use */
    cpus[0].cpu_id = 0;
    ncpu = 1;
    wrmsr(MSR_GS_BASE, (u64)&cpus[0]);
    wrmsr(MSR_KERNEL_GS_BASE, (u64)&cpus[0]);

    klog("MEM", "initializing buddy allocator");
    kinit(hhdm_request.response->offset);

    struct limine_memmap_response *memmap_response = memmap_request.response;
    struct limine_memmap_entry **entries = memmap_response->entries;
    u64 available_mem = 0;
    for (u64 i = 0; i < memmap_response->entry_count; i++) {
        struct limine_memmap_entry *entry = entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            freerange(entry->base, entry->base + entry->length);
            available_mem += entry->length;
        }
    }
    buddy_enable_lock();
    klog_ok("MEM", "%u MB available", (u32)(available_mem / (1024 * 1024)));

    init_gdt();
    wrmsr(MSR_GS_BASE, (u64)&cpus[0]);
    wrmsr(MSR_KERNEL_GS_BASE, (u64)&cpus[0]);
    klog_ok("GDT", "segments loaded");

    init_syscall();
    proc_init();
    klog_ok("SYSCALL", "MSRs configured");

    for (u64 i = 0; i < memmap_response->entry_count; i++) {
        struct limine_memmap_entry *entry = entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE &&
            entry->type != LIMINE_MEMMAP_BAD_MEMORY)
            map_mmio(entry->base, entry->length);
    }
    u64 rsdp_phys = (u64)rsdp_request.response->address;
    map_mmio(rsdp_phys, PAGE_SIZE);

    init_acpi(PHYS_TO_VIRT(rsdp_phys));
    klog_ok("ACPI", "tables parsed");

    pic_disable();
    lapic_init();
    ioapic_init();

    devfs_register_fb();

    ioapic_route_irq(0,  32, lapic_id());
    pit_stop();
    lapic_timer_periodic(32, 1000000);
    ioapic_route_irq(1,  33, lapic_id());
    ioapic_route_irq(12, 44, lapic_id());
    ioapic_route_irq(14, 46, lapic_id());
    ioapic_route_irq(15, 47, lapic_id());
    klog_ok("IRQ", "routes configured");

    ps2_init();
    sti();

    pci_scan();
    ahci_init();
    ata_init();

    vfs_init();
    ext2_init();
    initfs_init();
    devfs_init();
    klog_ok("VFS", "filesystems registered");

    /* initfs is the permanent root — always succeeds */
    vfs_mount("initfs", 0, "/", 0, 0);
    vfs_mkdir("/dev", 0755);   /* fallback /dev if no disk */

    /* if a block device is found, overlay ext2 at / */
    {
        struct blk_device *rootdev = blk_get("ahci0");
        if (!rootdev) rootdev = blk_get("ata0");
        if (rootdev) {
            if (vfs_mount("ext2", rootdev, "/", 0, 0) == VFS_OK)
                klog_ok("FS", "ext2 mounted at /");
            else
                klog_fail("FS", "ext2 mount failed");
        } else {
            klog("FS", "no block device — running from initfs only");
        }
    }

    {
        static const char *blk_names[] = {
            "ahci0","ahci1","ahci2","ahci3",
            "ata0","ata1","ata2","ata3", 0
        };
        for (int i = 0; blk_names[i]; i++) {
            struct blk_device *d = blk_get(blk_names[i]);
            if (d) devfs_register_blk(d);
        }
        if (vfs_mount("devfs", 0, "/dev", 0, 0) == VFS_OK)
            klog_ok("FS", "devfs mounted at /dev");
        else
            klog_fail("FS", "devfs mount failed");
    }

    struct limine_mp_response *smp_response = smp_request.response;
    if (smp_response) {
        u32 bsp_lapic_id = smp_response->bsp_lapic_id;
        cpus[0].apic_id = bsp_lapic_id;
        for (u64 i = 0; i < smp_response->cpu_count && ncpu < MAX_CPUS; i++) {
            struct limine_mp_info *cpu = smp_response->cpus[i];
            if (cpu->lapic_id == bsp_lapic_id) continue;
            cpus[ncpu].apic_id = cpu->lapic_id;
            cpus[ncpu].cpu_id  = ncpu;
            ncpu++;
        }
        for (u64 i = 0; i < smp_response->cpu_count; i++) {
            struct limine_mp_info *cpu = smp_response->cpus[i];
            if (cpu->lapic_id == bsp_lapic_id) continue;
            __atomic_store_n(&cpu->goto_address, ap_entry, __ATOMIC_SEQ_CST);
        }
        u64 expected_aps = ncpu - 1;
        while (__atomic_load_n(&ap_started, __ATOMIC_SEQ_CST) < expected_aps)
            ;
        klog_ok("SMP", "%u CPU(s) online", ncpu);
    }

    struct proc *p = proc_create("/bin/init");
    if (p) klog_ok("PROC", "init started (pid %u)", p->pid);
    else   klog_fail("PROC", "no init found at /bin/init");

    puts("\r\n\r\n\033[1;32m  kernel ready\033[0m\r\n\r\n");
    scheduler();
}

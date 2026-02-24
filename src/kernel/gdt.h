#pragma once
#include "types.h"

#define KERNEL_CS 0x28
#define KERNEL_DS 0x30
#define USER_DS   0x3B  // 0x38 | RPL=3
#define USER_CS   0x43  // 0x40 | RPL=3
#define TSS_SEL   0x48

struct tss {
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist1;
    u64 ist2;
    u64 ist3;
    u64 ist4;
    u64 ist5;
    u64 ist6;
    u64 ist7;
    u64 reserved2;
    u16 reserved3;
    u16 iopb_offset;
} __attribute__((packed));

void init_gdt(void);
void init_gdt_ap(u8 cpu_id);
void tss_set_rsp0(u64 rsp0);

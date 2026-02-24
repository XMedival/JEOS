#pragma once
#include "types.h"

#define IDT_INTERRUPT_GATE 0x8E  // present, ring 0, interrupt gate
#define IDT_TRAP_GATE      0x8F  // present, ring 0, trap gate
#define IDT_ENTRIES        256

// Kernel code segment selector
#define KERNEL_CS          0x28

// IRQ vector numbers (remapped to start at 32)
#define IRQ_TIMER          32
#define IRQ_KEYBOARD       33
#define IRQ_CASCADE        34
#define IRQ_COM2           35
#define IRQ_COM1           36
#define IRQ_LPT2           37
#define IRQ_FLOPPY         38
#define IRQ_LPT1           39
#define IRQ_RTC            40
#define IRQ_FREE1          41
#define IRQ_FREE2          42
#define IRQ_FREE3          43
#define IRQ_MOUSE          44
#define IRQ_FPU            45
#define IRQ_ATA_PRIMARY    46
#define IRQ_ATA_SECONDARY  47
#define IRQ_AHCI           48

// ISR stub macros (moved from x86.h for logical grouping)
#define ISR_STUB(num)                           \
    __attribute((naked)) void isr##num(void) {  \
        asm volatile(                           \
        "push $0\n"                             \
        "push $" #num "\n"                      \
        "jmp isr_common\n"                      \
        );                                      \
    }

#define ISR_STUB_ERR(num)                       \
    __attribute((naked)) void isr##num(void) {  \
        asm volatile(                           \
        "push $" #num "\n"                      \
        "jmp isr_common\n"                      \
        );                                      \
    }

struct idt_entry {
  u16	offset_1;
  u16	selector;
  u8	ist;
  u8	type_attr;
  u16	offset_2;
  u32	offset_3;
  u32	zero;			//reserved
} __attribute__((packed));

struct idt_ptr {
  u16 limit;
  u64 base;
} __attribute__((packed));

struct trap_frame {
  u64 r15, r14, r13, r12, r11, r10, r9, r8;
  u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
  u64 int_no, error_code;
  u64 rip, cs, rflags, rsp, ss;
};

void exception_handler(struct trap_frame *frame);

void init_idt(void);
void load_idt(void);
void idt_set_gate(u8 num, u64 handler, u8 type);

#include "idt.h"
#include "print.h"
#include "ring.h"
#include "x86.h"
#include "apic.h"
#include "panic.h"
#include "ps2.h"
#include "ahci.h"
#include "ata.h"
#include "proc.h"
#include "kconsole.h"

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idtr;

static i32 cursor_x = 0, cursor_y = 0;
static i32 old_cursor_x = -1, old_cursor_y = -1;
static int cursor_init = 0;
static void *fb_addr_cache = NULL;
static u32 fb_pitch_cache = 0;
static u32 fb_width_cache = 0;
static u32 fb_height_cache = 0;

static void draw_cursor(i32 x, i32 y, int clear) {
    if (!fb_addr_cache) {
        fb_addr_cache = kconsole_get_addr();
        if (fb_addr_cache) {
            kconsole_get_info(&fb_width_cache, &fb_height_cache, &fb_pitch_cache, NULL);
        }
    }
    if (!fb_addr_cache) return;
    
    volatile u32 *fb = (volatile u32 *)fb_addr_cache;
    u32 pitch_u32 = fb_pitch_cache / 4;
    
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= (i32)fb_width_cache) x = fb_width_cache - 1;
    if (y >= (i32)fb_height_cache) y = fb_height_cache - 1;
    
    int size = 32;
    for (int dy = -size; dy < size; dy++) {
        for (int dx = -size; dx < size; dx++) {
            if (dx*dx + dy*dy < size*size) {
                u32 py = (u32)(y + dy);
                u32 px = (u32)(x + dx);
                fb[py * pitch_u32 + px] = clear ? 0 : 0x00FF00; // Green cursor
            }
        }
    }
}

void timer_handler() {
    static u32 ticks = 0;
    ticks++;
    yield();
}    

// Common handler that all stubs jump to
__attribute__((naked)) void isr_common(void) {
    asm volatile(
        // swapgs if coming from user mode (CS RPL != 0)
        "testl $3, 24(%%rsp)\n"
        "jz 1f\n"
        "swapgs\n"
        "1:\n"

        // Save all registers
        "push %%rax\n"
        "push %%rbx\n"
        "push %%rcx\n"
        "push %%rdx\n"
        "push %%rsi\n"
        "push %%rdi\n"
        "push %%rbp\n"
        "push %%r8\n"
        "push %%r9\n"
        "push %%r10\n"
        "push %%r11\n"
        "push %%r12\n"
        "push %%r13\n"
        "push %%r14\n"
        "push %%r15\n"

        // Call C handler with frame pointer
        "mov %%rsp, %%rdi\n"
        "call exception_handler\n"

        // trapret: restores registers and returns to user/kernel mode
        // Also used as entry point for newly created processes
        ".globl trapret\n"
        "trapret:\n"

        // Restore all registers
        "pop %%r15\n"
        "pop %%r14\n"
        "pop %%r13\n"
        "pop %%r12\n"
        "pop %%r11\n"
        "pop %%r10\n"
        "pop %%r9\n"
        "pop %%r8\n"
        "pop %%rbp\n"
        "pop %%rdi\n"
        "pop %%rsi\n"
        "pop %%rdx\n"
        "pop %%rcx\n"
        "pop %%rbx\n"
        "pop %%rax\n"

        // Remove error code and interrupt number
        "add $16, %%rsp\n"

        // swapgs if returning to user mode
        "testl $3, 8(%%rsp)\n"
        "jz 2f\n"
        "swapgs\n"
        "2:\n"
        "iretq\n"
        ::: "memory");
}

// ISR stubs - exceptions without error code
ISR_STUB(0) // Divide by zero
ISR_STUB(1) // Debug
ISR_STUB(2) // NMI
ISR_STUB(3) // Breakpoint
ISR_STUB(4) // Overflow
ISR_STUB(5) // Bound range exceeded
ISR_STUB(6) // Invalid opcode
ISR_STUB(7) // Device not available

// Exceptions with error code
ISR_STUB_ERR(8)  // Double fault
ISR_STUB(9)      // Coprocessor segment overrun (reserved)
ISR_STUB_ERR(10) // Invalid TSS
ISR_STUB_ERR(11) // Segment not present
ISR_STUB_ERR(12) // Stack-segment fault
ISR_STUB_ERR(13) // General protection fault
ISR_STUB_ERR(14) // Page fault
ISR_STUB(15)     // Reserved
ISR_STUB(16)     // x87 FPU error
ISR_STUB_ERR(17) // Alignment check
ISR_STUB(18)     // Machine check
ISR_STUB(19)     // SIMD floating-point
ISR_STUB(20)     // Virtualization
ISR_STUB_ERR(21) // Control protection
ISR_STUB(22)
ISR_STUB(23)
ISR_STUB(24)
ISR_STUB(25)
ISR_STUB(26)
ISR_STUB(27)
ISR_STUB(28)
ISR_STUB(29)
ISR_STUB_ERR(30) // Security exception
ISR_STUB(31)

// Hardware IRQs (32-47)
ISR_STUB(32)
ISR_STUB(33)  // Keyboard
ISR_STUB(34)
ISR_STUB(35)
ISR_STUB(36)
ISR_STUB(37)
ISR_STUB(38)
ISR_STUB(39)
ISR_STUB(40)
ISR_STUB(41)
ISR_STUB(42)
ISR_STUB(43)
ISR_STUB(44)
ISR_STUB(45)
ISR_STUB(46)  // ATA primary
ISR_STUB(47)  // ATA secondary
ISR_STUB(48)  // AHCI MSI

// Spurious interrupt handler (no EOI needed)
__attribute__((naked)) void isr_spurious(void) {
    asm volatile("iretq");
}

// Declare the ISR functions
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);
extern void isr32(void);
extern void isr33(void);
extern void isr34(void);
extern void isr35(void);
extern void isr36(void);
extern void isr37(void);
extern void isr38(void);
extern void isr39(void);
extern void isr40(void);
extern void isr41(void);
extern void isr42(void);
extern void isr43(void);
extern void isr44(void);
extern void isr45(void);
extern void isr46(void);
extern void isr47(void);
extern void isr48(void);

static void (*isr_table[49])(void) = {
    isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,  isr8,  isr9,  isr10,
    isr11, isr12, isr13, isr14, isr15, isr16, isr17, isr18, isr19, isr20, isr21,
    isr22, isr23, isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31,
    isr32, isr33, isr34, isr35, isr36, isr37, isr38, isr39, isr40, isr41,
    isr42, isr43, isr44, isr45, isr46, isr47, isr48};

void idt_set_gate(u8 num, u64 handler, u8 type) {
    idt[num].offset_1 = handler & 0xFFFF;
    idt[num].selector = KERNEL_CS;
    idt[num].ist = 0;
    idt[num].type_attr = type;
    idt[num].offset_2 = (handler >> 16) & 0xFFFF;
    idt[num].offset_3 = (handler >> 32) & 0xFFFFFFFF;
    idt[num].zero = 0;
}

void init_idt(void) {
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (u64)&idt;

    // Clear IDT
    for (u32 i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, 0, 0);
    }

    // Set up exception handlers (0-31) and IRQ handlers (32-48)
    for (u32 i = 0; i < 49; i++) {
        idt_set_gate(i, (u64)isr_table[i], IDT_INTERRUPT_GATE);
    }

    // Spurious interrupt handler (vector 0xFF)
    extern void isr_spurious(void);
    idt_set_gate(0xFF, (u64)isr_spurious, IDT_INTERRUPT_GATE);

    asm volatile("lidt %0" : : "m"(idtr));
}

void load_idt(void) {
    asm volatile("lidt %0" : : "m"(idtr));
}

// Scancode set 1 to ASCII (US keyboard layout)
static const char scancode_table[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

void exception_handler(struct trap_frame *frame) {
    switch (frame->int_no) {
    case IRQ_TIMER:
        lapic_eoi();
	timer_handler();
        break;
    case IRQ_KEYBOARD: {
        kbd_interrupt();
        lapic_eoi();
        break;
    }
    case IRQ_MOUSE: {
        mouse_interrupt();
        lapic_eoi();
        break;
    }
    case IRQ_ATA_PRIMARY:
        ata_irq_handler(0);
        lapic_eoi();
        break;
    case IRQ_ATA_SECONDARY:
        ata_irq_handler(1);
        lapic_eoi();
        break;
    case IRQ_AHCI:
        ahci_irq_handler();
        lapic_eoi();
        break;
    case 0x0:
      panic("DIVISION ERROR", frame);
    case 0x1:
        panic("DEBUG?", frame);
    case 0x2:
        panic("NMI", frame);
    case 0x3:
        panic("BREAKPOINT", frame);
    case 0x4:
        panic("OVERFLOW", frame);
    case 0x5:
        panic("BOUND RANGE", frame);
    case 0x6:
        panic("INVALID OPCODE", frame);
    case 0x7:
      panic("DEVICE NOT AVAILABLE", frame);
    case 0x8:
        panic("DOUBLE FAULT", frame);
    case 0x9:
        panic("WHY ARE WE HERE?!?! COPROCESSOR", frame);
    case 0xA:
        panic("INVALID TSS", frame);
    case 0xB:
        panic("SEGMENT NOT PRESENT", frame);
    case 0xC:
        panic("STACK-SEGMENT FAULT", frame);
    case 0xD:
        panic("GENERAL PROTECTION FAULT", frame);
    case 0xE:
        panic("PAGE FAULT", frame);
	// 0xF is reserved
    case 0x10:
        panic("x87 FPU EXCEPTION", frame);
    case 0x11:
        panic("ALIGNMENT CHECK", frame);
    case 0x12:
        panic("MACHINE CHECK", frame);
    case 0x13:
        panic("SIMD FPU EXCEPTION", frame);
    case 0x14:
        panic("VIRT EXCEPTION", frame);
    case 0x15:
        panic("CONTROL PROTECTION EXCEPTION", frame);
        // 0x16..0x1B are reserved
    case 0x1C:
        panic("HYPERVISOR INJECTION", frame);
    case 0x1D:
        panic("VMM COMM EXCEPTION", frame);
    case 0x1E:
        panic("SECURITY EXCEPTION", frame);
	// 0x1F is reserved
    default:
      panic(0, frame);
    }
}

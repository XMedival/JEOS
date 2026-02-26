#include "panic.h"
#include "print.h"
#include "x86.h"

static inline u8 canonical(u64 a) {
    // x86_64 canonical addresses: bits 63..48 replicate bit 47
    u64 top = a >> 48;
    return (top == 0x0000ULL) || (top == 0xFFFFULL);
}

static inline u8 aligned16(u64 a) {
    return (a & 0x7) == 0; // at least 8-byte aligned for our loads
}

static void backtrace_from(u64 start_rbp, u64 start_rip, int max_frames) {
    puts("Backtrace:\r\n");

    // Print the faulting RIP first (it is *not* a "return address", but it's useful)
    printf("  #0  rip=%p\r\n", (void*)start_rip);

    u64 rbp = start_rbp;

    for (int i = 1; i < max_frames; i++) {
        if (!canonical(rbp) || !aligned16(rbp) || rbp == 0)
            break;

        // frame layout: [0]=prev_rbp, [1]=ret_addr
        volatile u64 *frame = (volatile u64 *)rbp;

        u64 next_rbp = frame[0];
        u64 ret_addr = frame[1];

        if (!canonical(ret_addr))
            break;

        printf("  #%d  rip=%p  rbp=%p\r\n", i, (void*)ret_addr, (void*)rbp);

        // stop on corruption / loops
        if (next_rbp <= rbp)
            break;

        rbp = next_rbp;
    }
}

void __panic(const char *msg, struct trap_frame *frame) {
    if (msg || frame)
        puts("======================== PANIC "
             "========================\r\n");
    if (msg) {
        printf("%^56s\r\n", msg);
    }
    if (frame) {
      u64 cr0, cr2, cr3;
	asm volatile("mov %%cr0, %0" : "=r"(cr0));
	asm volatile("mov %%cr2, %0" : "=r"(cr2));
	asm volatile("mov %%cr3, %0" : "=r"(cr3));
        printf("EXCEPTION: %p ERRNO: %p\r\n", frame->int_no, frame->error_code);
        printf("RAX:	   %p RBX:   %p\r\n", frame->rax, frame->rbx);
        printf("RCX:	   %p RDX:   %p\r\n", frame->rcx, frame->rdx);
        printf("RIP:	   %p CR0:   %p\r\n", frame->rip, cr0);
        printf("RSP:	   %p RBP:   %p\r\n", frame->rsp, frame->rbp);
        printf("CR2:	   %p CR3:   %p\r\n", cr2, cr3);
        backtrace_from(frame->rbp, frame->rip, 32);
        puts("=============================="
             "=========================\r\n");
    }
    for (;;) {
        cli();
        hlt();
    }
}

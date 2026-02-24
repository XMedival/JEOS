#include "panic.h"
#include "print.h"
#include "x86.h"

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
        puts("=============================="
             "=========================\r\n");
    }
    for (;;) {
        cli();
        hlt();
    }
}

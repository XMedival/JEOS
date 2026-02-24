global syscall_entry
extern syscall_handler

section .text

syscall_entry:
    ; On entry from syscall instruction:
    ;   RCX = user RIP, R11 = user RFLAGS, RAX = syscall number
    ;   RDI,RSI,RDX,R10,R8,R9 = args 1-6
    ;   Interrupts are masked (FMASK cleared IF)

    ; Swap to kernel GS (per-CPU struct)
    swapgs

    ; Save user RSP, switch to kernel stack (via per-CPU struct)
    mov [gs:8], rsp             ; cpu->scratch_rsp = user RSP
    mov rsp, [gs:0]             ; RSP = cpu->kernel_rsp

    ; Save user return state and callee-preserved registers
    push qword [gs:8]           ; user RSP
    push rcx                    ; user RIP
    push r11                    ; user RFLAGS
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    ; Save argument registers (caller-saved, user expects preserved)
    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9

    ; 15 pushes = 120 bytes from 16-aligned kernel_rsp
    ; 120 % 16 = 8, so RSP is 8 mod 16. Need sub 8 for alignment.
    sub rsp, 8

    ; Map syscall convention to C calling convention:
    ;   syscall: RAX=num, RDI=a1, RSI=a2, RDX=a3, R10=a4, R8=a5
    ;   C call:  RDI=num, RSI=a1, RDX=a2, RCX=a3, R8=a4, R9=a5
    mov r9, r8                  ; a5 -> r9
    mov r8, r10                 ; a4 -> r8
    mov rcx, rdx                ; a3 -> rcx
    mov rdx, rsi                ; a2 -> rdx
    mov rsi, rdi                ; a1 -> rsi
    mov rdi, rax                ; num -> rdi

    call syscall_handler
    ; RAX = return value

    add rsp, 8                  ; remove alignment padding

    ; Restore argument registers
    pop r9
    pop r8
    pop r10
    pop rdx
    pop rsi
    pop rdi

    ; Restore callee-preserved registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    pop r11                     ; user RFLAGS
    pop rcx                     ; user RIP
    mov rsp, [rsp]              ; restore user RSP

    ; Swap back to user GS
    swapgs
    o64 sysret

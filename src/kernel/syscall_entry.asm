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

    ; Save registers to current_proc->tf for fork() to use
    ; proc struct layout: pid,ppid,state,exit_code,pml4,kstack,context,tf,brk,name,files
    ; tf is at offset 40 in struct proc
    ; Stack layout at this point (rsp points to alignment padding):
    ;   rsp+8:   r9    rsp+16:  r8    rsp+24:  r10
    ;   rsp+32:  rdx   rsp+40:  rsi   rsp+48:  rdi
    ;   rsp+56:  r15   rsp+64:  r14   rsp+72:  r13
    ;   rsp+80:  r12   rsp+88:  rbp   rsp+96:  rbx
    ;   rsp+104: r11   rsp+112: rcx   rsp+120: user_rsp
    mov rax, [gs:16]            ; rax = cpu->proc (current_proc)
    test rax, rax
    jz .skip_tf_save
    ; trap_frame layout: r15,r14,r13,r12,r11,r10,r9,r8, rbp,rdi,rsi,rdx,rcx,rbx,rax, int_no,err, rip,cs,rflags,rsp,ss
    mov rbx, [rsp+56]
    mov [rax + 40], rbx         ; tf->r15
    mov rbx, [rsp+64]
    mov [rax + 48], rbx         ; tf->r14
    mov rbx, [rsp+72]
    mov [rax + 56], rbx         ; tf->r13
    mov rbx, [rsp+80]
    mov [rax + 64], rbx         ; tf->r12
    mov rbx, [rsp+104]
    mov [rax + 72], rbx         ; tf->r11
    mov rbx, [rsp+24]
    mov [rax + 80], rbx         ; tf->r10
    mov rbx, [rsp+8]
    mov [rax + 88], rbx         ; tf->r9
    mov rbx, [rsp+16]
    mov [rax + 96], rbx         ; tf->r8
    mov rbx, [rsp+88]
    mov [rax + 104], rbx        ; tf->rbp
    mov rbx, [rsp+48]
    mov [rax + 112], rbx        ; tf->rdi
    mov rbx, [rsp+40]
    mov [rax + 120], rbx        ; tf->rsi
    mov rbx, [rsp+32]
    mov [rax + 128], rbx        ; tf->rdx
    mov rbx, [rsp+112]
    mov [rax + 136], rbx        ; tf->rcx (user RIP)
    mov rbx, [rsp+96]
    mov [rax + 144], rbx        ; tf->rbx
    mov [rax + 160], rbx        ; tf->rip
    mov rbx, 0x20
    mov [rax + 168], rbx        ; tf->cs = USER_CS
    mov rbx, [rsp+104]
    mov [rax + 176], rbx        ; tf->rflags
    mov rbx, [rsp+120]
    mov [rax + 184], rbx        ; tf->rsp (user RSP)
    mov rbx, 0x28
    mov [rax + 192], rbx        ; tf->ss = USER_DS
.skip_tf_save:

    ; Map syscall convention to C calling convention:
    ;   syscall: RAX=num, RDI=a1, RSI=a2, RDX=a3, R10=a4, R8=a5
    ;   C call:  RDI=num, RSI=a1, RDX=a2, RCX=a3, R8=a4, R9=a5
    mov r9, r8                  ; a5 -> r9
    mov r8, r10                 ; a4 -> r8
    mov rcx, r10                ; a3 -> rcx (R10 holds a3!)
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

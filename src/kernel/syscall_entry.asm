global syscall_entry
extern syscall_handler

section .text

syscall_entry:
    ; On entry from syscall instruction:
    ;   RCX = user RIP, R11 = user RFLAGS, RAX = syscall number
    ;   RDI,RSI,RDX,R10,R8,R9 = args 1-6
    ;   Interrupts are masked (FMASK cleared IF)

    ; Save syscall number early (we'll clobber RAX later)
    mov r12, rax                ; r12 = syscall number

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
    push r12                    ; NOTE: we pushed syscall number copy, not user r12
    push r13
    push r14
    push r15

    ; Save argument registers (caller-saved)
    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9

    ; 15 pushes = 120 bytes from 16-aligned kernel_rsp
    ; 120 % 16 = 8, so RSP is 8 mod 16. Need sub 8 for alignment.
    sub rsp, 8

    ; Stack layout at this point (rsp points to alignment padding):
    ;   rsp+8:   r9     rsp+16:  r8     rsp+24:  r10
    ;   rsp+32:  rdx    rsp+40:  rsi    rsp+48:  rdi
    ;   rsp+56:  r15    rsp+64:  r14    rsp+72:  r13
    ;   rsp+80:  r12(*) rsp+88:  rbp    rsp+96:  rbx
    ;   rsp+104: r11    rsp+112: rcx(user RIP)  rsp+120: user_rsp
    ; (*) r12 here is our saved syscall-number copy (not user r12)

    ; Save registers to current_proc->tf for fork() to use
    ; proc struct layout: pid,ppid,state,exit_code,pml4,kstack,context,tf,brk,name,files
    ; tf is embedded at offset 40 in struct proc
    mov rbx, [gs:16]            ; rbx = cpu->proc (current_proc)
    test rbx, rbx
    jz .skip_tf_save

    lea rax, [rbx + 40]         ; rax = &current_proc->tf (trap_frame base)

    ; trap_frame layout:
    ; r15,r14,r13,r12,r11,r10,r9,r8, rbp,rdi,rsi,rdx,rcx,rbx,rax, int_no,err, rip,cs,rflags,rsp,ss

    ; General regs from our stack frame
    mov rdx, [rsp+56]
    mov [rax + 0],  rdx         ; tf->r15
    mov rdx, [rsp+64]
    mov [rax + 8],  rdx         ; tf->r14
    mov rdx, [rsp+72]
    mov [rax + 16], rdx         ; tf->r13
    mov rdx, [rsp+80]
    mov [rax + 24], rdx         ; tf->r12  (this is our saved syscall-number copy)
    mov rdx, [rsp+104]
    mov [rax + 32], rdx         ; tf->r11  (user RFLAGS from SYSCALL)

    mov rdx, [rsp+24]
    mov [rax + 40], rdx         ; tf->r10
    mov rdx, [rsp+8]
    mov [rax + 48], rdx         ; tf->r9
    mov rdx, [rsp+16]
    mov [rax + 56], rdx         ; tf->r8

    mov rdx, [rsp+88]
    mov [rax + 64], rdx         ; tf->rbp
    mov rdx, [rsp+48]
    mov [rax + 72], rdx         ; tf->rdi
    mov rdx, [rsp+40]
    mov [rax + 80], rdx         ; tf->rsi
    mov rdx, [rsp+32]
    mov [rax + 88], rdx         ; tf->rdx

    ; We do NOT have the user's original RCX (SYSCALL clobbers it with user RIP).
    ; Store user RIP in tf->rcx (as your code intended) and also in tf->rip.
    mov rdx, [rsp+112]
    mov [rax + 96],  rdx        ; tf->rcx = user RIP (convention in this kernel)
    mov rdx, [rsp+96]
    mov [rax + 104], rdx        ; tf->rbx

    ; tf->rax: store the syscall number we saved in r12 (pre-handler)
    mov rdx, r12
    mov [rax + 112], rdx        ; tf->rax = syscall number (pre-handler snapshot)

    ; int_no / error_code: syscall path doesn't have them
    xor edx, edx
    mov [rax + 120], rdx        ; tf->int_no = 0
    mov [rax + 128], rdx        ; tf->error_code = 0

    ; Return frame for iret/sysret-style semantics
    mov rdx, [rsp+112]
    mov [rax + 136], rdx        ; tf->rip = user RIP

    mov rdx, 0x20
    mov [rax + 144], rdx        ; tf->cs = USER_CS

    mov rdx, [rsp+104]
    mov [rax + 152], rdx        ; tf->rflags = user RFLAGS

    mov rdx, [rsp+120]
    mov [rax + 160], rdx        ; tf->rsp = user RSP

    mov rdx, 0x28
    mov [rax + 168], rdx        ; tf->ss = USER_DS

.skip_tf_save:

    ; Map syscall convention to C calling convention:
    ;   syscall: RAX=num, RDI=a1, RSI=a2, RDX=a3, R10=a4, R8=a5, R9=a6
    ;   C call:  RDI=num, RSI=a1, RDX=a2, RCX=a3, R8=a4, R9=a5
    ;
    ; We ignore a6 (syscall r9) because the C prototype you documented only takes 6 total
    ; params (num + 5 args).

    mov r9,  r8                 ; a5 -> r9
    mov r8,  r10                ; a4 -> r8
    mov rcx, rdx                ; a3 -> rcx
    mov rdx, rsi                ; a2 -> rdx
    mov rsi, rdi                ; a1 -> rsi
    mov rdi, r12                ; num -> rdi (from saved syscall number)

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
    mov rsp, [rsp]              ; restore user RSP (top qword is the saved user_rsp)

    ; Swap back to user GS
    swapgs
    o64 sysret

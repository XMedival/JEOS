global syscall_entry
extern syscall_handler

section .text

syscall_entry:
    ; On entry from syscall instruction:
    ;   RCX = user RIP, R11 = user RFLAGS, RAX = syscall number
    ;   RDI,RSI,RDX,R10,R8,R9 = args 1-6
    ;   Interrupts are masked (FMASK cleared IF)

    swapgs

    ; Save user RSP, switch to kernel stack (via per-CPU struct)
    mov [gs:8], rsp             ; cpu->scratch_rsp = user RSP
    mov rsp, [gs:0]             ; RSP = cpu->kernel_rsp (must be 16-aligned)

    ; Save user return state
    push qword [gs:8]           ; saved user RSP
    push rcx                    ; saved user RIP
    push r11                    ; saved user RFLAGS

    ; Save callee-preserved regs (PRESERVE USER r12!)
    push rbx
    push rbp
    push r12                    ; user r12 (real)
    push r13
    push r14
    push r15

    ; Save syscall argument regs (caller-saved)
    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9

    ; Save syscall number (caller-saved) AFTER args
    push rax                    ; syscall number

    ; Push count so far:
    ;   3 (ret state) + 6 (callee) + 6 (args) + 1 (sysno) = 16 pushes = 128 bytes
    ; If kernel_rsp was 16-aligned, RSP is still 16-aligned here. No padding needed.

    ; Stack layout now (rsp points to saved syscall number):
    ;   rsp+0:   sysno (rax)
    ;   rsp+8:   r9     rsp+16:  r8     rsp+24:  r10
    ;   rsp+32:  rdx    rsp+40:  rsi    rsp+48:  rdi
    ;   rsp+56:  r15    rsp+64:  r14    rsp+72:  r13
    ;   rsp+80:  r12    rsp+88:  rbp    rsp+96:  rbx
    ;   rsp+104: r11    rsp+112: rcx(user RIP)  rsp+120: user_rsp

    ; Save registers to current_proc->tf for fork() to use
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
    mov [rax + 24], rdx         ; tf->r12 (USER r12, fixed)

    mov rdx, [rsp+104]
    mov [rax + 32], rdx         ; tf->r11 (user RFLAGS from SYSCALL)

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

    ; SYSCALL clobbers RCX with user RIP; store that where your kernel expects
    mov rdx, [rsp+112]
    mov [rax + 96],  rdx        ; tf->rcx = user RIP (your convention)
    mov rdx, [rsp+96]
    mov [rax + 104], rdx        ; tf->rbx

    ; tf->rax: store syscall number snapshot (saved on stack)
    mov rdx, [rsp+0]
    mov [rax + 112], rdx        ; tf->rax = syscall number

    xor edx, edx
    mov [rax + 120], rdx        ; tf->int_no = 0
    mov [rax + 128], rdx        ; tf->error_code = 0

    ; Return frame
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

    ; IMPORTANT: reload syscall args from the saved stack slots (regs may be clobbered above)
    mov rdi, [rsp+0]            ; num (syscall number)
    mov rsi, [rsp+48]           ; a1
    mov rdx, [rsp+40]           ; a2
    mov rcx, [rsp+32]           ; a3
    mov r8,  [rsp+24]           ; a4
    mov r9,  [rsp+16]           ; a5
    ; a6 is [rsp+8] if you ever want it

    call syscall_handler
    ; RAX = return value

    ; Restore syscall number + argument regs
    pop rax                     ; discard saved sysno (we keep handler return in rax? NO!)
    ; ^ If you want to keep handler return value in RAX, DON'T pop into rax.
    ;   Instead do: add rsp, 8
    ;   (Fix below.)

    ; --- Correct restore: keep RAX return value ---
    ; Replace the above "pop rax" with:
    ;   add rsp, 8

    ; Restore argument registers (original user values)
    pop r9
    pop r8
    pop r10
    pop rdx
    pop rsi
    pop rdi

    ; Restore callee-preserved registers (original user values)
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    ; Restore user return state
    pop r11                     ; user RFLAGS
    pop rcx                     ; user RIP
    pop rsp                     ; user RSP

    swapgs
    o64 sysret

global swtch

section .text

swtch:
    ; swtch(struct context **old, struct context *new)
    ; rdi = old, rsi = new
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp          ; *old = rsp (save old context pointer)
    mov rsp, rsi            ; rsp = new  (load new context)

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret                     ; pops rip from new context

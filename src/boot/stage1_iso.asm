; org 0x7C00
[BITS 16]

%define STAGE2_SIZE 2

start:
    mov dl, [boot_disk]
    xor ax, ax
    mov cs, ax
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov ax, 0x7C00
    mov sp, ax
    mov bp, ax

    ; reset disk
    xor ax, ax
    mov dl, [boot_disk]
    int 0x13

    mov ah, 2
    mov al, STAGE2_SIZE
    mov ch, 0
    mov cl, 5
    mov dh, 0
    mov dl, [boot_disk]
    mov bx, 0x1000
    int 0x13

    jc .fail

    jmp 0x0000:0x1000

.fail:
    cli
    hlt
    jmp .fail

boot_disk: db 0

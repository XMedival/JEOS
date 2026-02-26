# AGENTS.md

## Project
x86-64 hobby OS kernel with Limine bootloader.

## Build
```bash
make        # build ISO
make run    # run in QEMU
make debug  # debug with GDB
make clean  # clean build/
```

## Architecture
- **Kernel**: `src/kernel/` - x86-64 kernel (C + inline asm)
- **Userspace**: `src/user/` - init, jesh shell
- **Filesystem**: ext2 built into fs.img
- **Bootloader**: Limine (BIOS + UEFI)

## Key Files
- `src/kernel/main.c` - kernel entry
- `src/kernel/linker.ld` - kernel linker script
- `src/user/link.ld` - userspace linker script
- `src/user/include/ulib.h` - userspace libc

## Notes
- GCC with `-ffreestanding -mno-red-zone -mcmodel=kernel`
- Syscall interface via kernel interrupts
- Use `$(BUILD)/kernel.elf` for GDB debugging

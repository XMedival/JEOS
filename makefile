# ── Toolchain ─────────────────────────────────────────────
CC      := gcc
LD      := ld
NASM    := nasm

# ── Directories ───────────────────────────────────────────
SRC_KERNEL  := src/kernel
SRC_USER    := src/user
BUILD       := build
ISO_DIR     := $(BUILD)/isodir

# ── Kernel sources ────────────────────────────────────────
KERN_C     := $(wildcard $(SRC_KERNEL)/*.c)
KERN_ASM   := $(wildcard $(SRC_KERNEL)/*.asm)
KERN_OBJ   := $(patsubst $(SRC_KERNEL)/%.c,$(BUILD)/kernel/%.o,$(KERN_C)) \
              $(patsubst $(SRC_KERNEL)/%.asm,$(BUILD)/kernel/%.o,$(KERN_ASM))

# ── Userspace programs (subdirs of src/user/, skip include/) ──
USER_PROGS  := $(filter-out include,$(notdir $(shell find $(SRC_USER) -mindepth 1 -maxdepth 1 -type d 2>/dev/null)))
USER_BINS   := $(patsubst %,$(BUILD)/user/%,$(USER_PROGS))

# ── Flags ─────────────────────────────────────────────────
CFLAGS  := -ggdb -ffreestanding -mno-red-zone -mcmodel=kernel -fno-pic -fno-pie -nostdlib \
           -mno-sse -mno-sse2 -mno-mmx -mno-avx -fno-stack-protector \
           -Wall -Wextra -Isrc
LDFLAGS := -nostdlib -static -T src/kernel/linker.ld -z max-page-size=0x1000

# Userspace: freestanding, static, no stdlib, x86-64 SysV ABI
UCFLAGS := -O2 -ffreestanding -fno-stack-protector -fno-pie -fno-pic -nostdlib \
           -mno-red-zone -mno-sse -mno-sse2 -fno-builtin -Wall -Wextra \
           -Isrc/user/include
ULDFLAGS := -nostdlib -static -z max-page-size=0x1000

NASMF   := -f elf64

# ── Outputs ───────────────────────────────────────────────
KERNEL  := $(BUILD)/kernel.elf
FS_IMG  := $(BUILD)/fs.img
ISO     := $(BUILD)/os.iso

# ── Limine files ─────────────────────────────────────────
LIMINE_DIR  := /usr/share/limine

# ══════════════════════════════════════════════════════════
#  Targets
# ══════════════════════════════════════════════════════════

.PHONY: all clean iso run fs kernel user dirs

all: iso

# ── Directory scaffolding ─────────────────────────────────
dirs:
	@mkdir -p $(BUILD)/kernel $(BUILD)/user $(ISO_DIR)/boot/limine $(ISO_DIR)/EFI/BOOT

# ── Kernel ────────────────────────────────────────────────
$(BUILD)/kernel/%.o: $(SRC_KERNEL)/%.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernel/%.o: $(SRC_KERNEL)/%.asm | dirs
	$(NASM) $(NASMF) $< -o $@

kernel: $(KERN_OBJ) | dirs
	$(LD) $(LDFLAGS) $(KERN_OBJ) -o $(KERNEL)

# ── Userspace programs ────────────────────────────────────
# Each program lives in src/user/<name>/  and may contain:
#   *.c  *.asm  (compiled and linked together)
# A per-program linker script src/user/<name>/link.ld is used if present,
# otherwise src/user/link.ld is the default fallback.

$(BUILD)/user/ulib.o: $(SRC_USER)/ulib.c | dirs
	$(CC) $(UCFLAGS) -c $< -o $@

define USER_PROG_template
_USRC_$(1) := $$(wildcard $(SRC_USER)/$(1)/*.c)
_UASM_$(1) := $$(wildcard $(SRC_USER)/$(1)/*.asm)
_UOBJ_$(1) := $$(patsubst $(SRC_USER)/$(1)/%.c,  $(BUILD)/user/$(1)_%.o, $$(_USRC_$(1))) \
              $$(patsubst $(SRC_USER)/$(1)/%.asm, $(BUILD)/user/$(1)_%.o, $$(_UASM_$(1))) \
              $(BUILD)/user/ulib.o
_ULD_$(1)  := $$(if $$(wildcard $(SRC_USER)/$(1)/link.ld), \
                   $(SRC_USER)/$(1)/link.ld, \
                   $(SRC_USER)/link.ld)

$(BUILD)/user/$(1)_%.o: $(SRC_USER)/$(1)/%.c | dirs
	$(CC) $(UCFLAGS) -c $$< -o $$@

$(BUILD)/user/$(1)_%.o: $(SRC_USER)/$(1)/%.asm | dirs
	$(NASM) $(NASMF) $$< -o $$@

$(BUILD)/user/$(1): $$(_UOBJ_$(1)) | dirs
	$(LD) $(ULDFLAGS) -T $$(_ULD_$(1)) $$(_UOBJ_$(1)) -o $$@
endef

$(foreach prog,$(USER_PROGS),$(eval $(call USER_PROG_template,$(prog))))

user: $(USER_BINS)

# ── Filesystem image ─────────────────────────────────────
FS_SIZE_MB := 64

fs: user | dirs
	dd if=/dev/zero of=$(FS_IMG) bs=1M count=$(FS_SIZE_MB) status=none
	mkfs.ext2 -b 4096 -L rootfs $(FS_IMG)
	debugfs -w $(FS_IMG) -R "mkdir /dev" 2>/dev/null || true
	debugfs -w $(FS_IMG) -R "mkdir /bin" 2>/dev/null || true
	@$(foreach bin,$(USER_BINS), \
	    echo "  install $(bin) -> /bin/$(notdir $(bin))" ; \
	    debugfs -w $(FS_IMG) -R "write $(bin) /bin/$(notdir $(bin))" 2>/dev/null || true ;)
	@echo "Created ext2 $(FS_IMG) ($(FS_SIZE_MB) MiB)"

# ── ISO (Limine BIOS + Plan 9 style fs.img) ──────────────
iso: kernel fs
	@mkdir -p $(ISO_DIR)/boot/limine $(ISO_DIR)/EFI/BOOT
	cp $(KERNEL)                          $(ISO_DIR)/kernel.elf
	cp limine.conf                        $(ISO_DIR)/boot/limine/limine.conf
	cp $(LIMINE_DIR)/limine-bios.sys      $(ISO_DIR)/boot/limine/
	cp $(LIMINE_DIR)/limine-bios-cd.bin   $(ISO_DIR)/boot/limine/
	cp $(LIMINE_DIR)/limine-uefi-cd.bin   $(ISO_DIR)/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI          $(ISO_DIR)/EFI/BOOT/
	cp $(FS_IMG)                          $(ISO_DIR)/fs.img
	xorriso -as mkisofs \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot \
		-boot-load-size 4 \
		-boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		-o $(ISO) $(ISO_DIR)
	limine bios-install $(ISO)
	@echo "ISO ready: $(ISO)"

# ── Run in QEMU ──────────────────────────────────────────
run: iso
	qemu-system-x86_64 -cdrom $(ISO) -serial stdio -m 3G \
		-drive file=$(FS_IMG),format=raw,if=ide,index=0 \
		-no-reboot -no-shutdown \
		-d int,cpu_reset -D /tmp/qemu.log

debug: iso
	qemu-system-x86_64 -cdrom $(ISO) -serial stdio -m 3G \
		-drive file=$(FS_IMG),format=raw,if=ide,index=0 \
		-no-reboot -no-shutdown \
		-d int,cpu_reset -D /tmp/qemu.log \
		-s -S &
	gdb build/kernel.elf \
		-ex "target remote :1234" \
		-ex "set confirm off"

# ── Clean ─────────────────────────────────────────────────
clean:
	rm -rf $(BUILD)/*

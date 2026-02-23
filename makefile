# ── Toolchain ─────────────────────────────────────────────
CC      := gcc
LD      := ld
OBJCOPY := objcopy
NASM    := nasm

# ── Directories ───────────────────────────────────────────
SRC_BOOT   := src/boot
SRC_KERNEL := src/kernel
BUILD      := build
ISO_DIR    := $(BUILD)/isodir
ISO_FAT    := $(ISO_DIR)/boot

# ── Source / Object lists ─────────────────────────────────
KERN_C     := $(wildcard $(SRC_KERNEL)/*.c)
KERN_ASM   := $(wildcard $(SRC_KERNEL)/*.asm)
KERN_OBJ   := $(patsubst $(SRC_KERNEL)/%.c,$(BUILD)/kernel/%.o,$(KERN_C)) \
              $(patsubst $(SRC_KERNEL)/%.asm,$(BUILD)/kernel/%.o,$(KERN_ASM))

# ── Flags ─────────────────────────────────────────────────
CFLAGS  := -ffreestanding -mno-red-zone -nostdlib -Wall -Wextra -O2 -Isrc
LDFLAGS := -nostdlib -T src/kernel/linker.ld
NASMF   := -f elf64

# ── Outputs ───────────────────────────────────────────────
STAGE1_ISO := $(BUILD)/stage1_iso.bin
STAGE1_MBR := $(BUILD)/stage1_mbr.bin
STAGE2     := $(BUILD)/stage2.bin
KERNEL     := $(BUILD)/kernel.bin
FS_IMG     := $(BUILD)/fs.img
ISO        := $(BUILD)/os.iso

# ══════════════════════════════════════════════════════════
#  Targets
# ══════════════════════════════════════════════════════════

.PHONY: all clean iso run fs bootloader kernel dirs

all: iso

# ── Directory scaffolding ─────────────────────────────────
dirs:
	@mkdir -p $(BUILD)/boot $(BUILD)/kernel $(ISO_FAT)

# ── Bootloader (flat binaries via NASM) ───────────────────
$(STAGE1_ISO): $(SRC_BOOT)/stage1_iso.asm | dirs
	$(NASM) -f bin $< -o $@

$(STAGE1_MBR): $(SRC_BOOT)/stage1_mbr.asm | dirs
	$(NASM) -f bin $< -o $@

$(STAGE2): $(SRC_BOOT)/stage2.asm | dirs
	$(NASM) -f bin $< -o $@

bootloader: $(STAGE1_ISO) $(STAGE2)

# ── Kernel ────────────────────────────────────────────────
$(BUILD)/kernel/%.o: $(SRC_KERNEL)/%.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernel/%.o: $(SRC_KERNEL)/%.asm | dirs
	$(NASM) $(NASMF) $< -o $@

kernel: $(KERN_OBJ) | dirs
	$(LD) $(LDFLAGS) $(KERN_OBJ) -o $(KERNEL)

# ── Filesystem image ─────────────────────────────────────
#  Creates a blank image; populate it with your own FS tool.
#  Adjust size (64 MiB default) as needed.
FS_SIZE_MB := 64

fs: | dirs
	dd if=/dev/zero of=$(FS_IMG) bs=1M count=$(FS_SIZE_MB) status=none
	@echo "Created blank $(FS_IMG) ($(FS_SIZE_MB) MiB)"

# ── ISO (Plan 9 style: FAT for boot + fs.img inside) ─────
#  Layout:
#    /boot/stage1.bin  ← El Torito boot image
#    /boot/stage2.bin  ← stage 2 bootloader
#    /boot/kernel.bin  ← kernel
#    /fs.img           ← filesystem image
iso: bootloader kernel fs
	@mkdir -p $(ISO_FAT)
	cp $(STAGE1_ISO) $(ISO_FAT)/stage1.bin
	cp $(STAGE2)     $(ISO_FAT)/stage2.bin
	cp $(KERNEL)     $(ISO_FAT)/kernel.bin
	cp $(FS_IMG)     $(ISO_DIR)/fs.img
	xorriso -as mkisofs \
		-b boot/stage1.bin \
		-no-emul-boot \
		-boot-load-size 4 \
		-o $(ISO) $(ISO_DIR)
	@echo "ISO ready: $(ISO)"

# ── Run in QEMU ──────────────────────────────────────────
run: iso
	qemu-system-x86_64 -cdrom $(ISO) -m 256M

# ── Clean ─────────────────────────────────────────────────
clean:
	rm -rf $(BUILD)/*
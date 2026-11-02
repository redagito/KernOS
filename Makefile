# KernOS Makefile — runs inside the build container (gcc:13) or WSL.
#
# Key rules:
#   * The kernel is C11, freestanding 64-bit, no red zone, SSE/MMX off
#     (they stay that way until FPU state saves exist on context
#     switches). C was chosen so the TinyCC endgame is honest.
#   * Boot stages are assembled with GNU as (no foreign assembler).
#   * Header dependencies are tracked with -MMD -MP. The old KernOS tree
#     rebuilt nothing when headers changed; silent stale objects are not
#     acceptable in a project whose only test is "does it boot".

MAKEFLAGS += -r -R

CC      := gcc
AS      := as
LD      := ld
OBJCOPY := objcopy

BUILD   := build
OBJ     := obj
INC     := include

TRAP_SELFTEST ?= 0

KFLAGS := -std=c11 -m64 -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-pie -fno-pic \
	-mno-red-zone -mno-mmx -mno-sse -mno-sse2 -msoft-float \
	-Wall -Wextra -O2 -g \
	-I$(INC) -MMD -MP \
	-DTRAP_SELFTEST=$(TRAP_SELFTEST)

TRAP_SELFTEST ?= 0

HOST_CFLAGS := -std=c11 -O2 -Wall -Wextra

# -----
# Kernel objects
# -----

KERNEL_OBJ := $(OBJ)/start64.o \
	$(OBJ)/main.o \
	$(OBJ)/vga.o \
	$(OBJ)/serial.o \
	$(OBJ)/cpu.o \
	$(OBJ)/trap.o \
	$(OBJ)/idt.o \
	$(OBJ)/irq.o \
	$(OBJ)/pit.o \
	$(OBJ)/kbd.o \
	$(OBJ)/sched.o \
	$(OBJ)/mem.o \
	$(OBJ)/pmm.o \
	$(OBJ)/page.o \
	$(OBJ)/heap.o \
	$(OBJ)/pci.o \
	$(OBJ)/ahci.o \
	$(OBJ)/fat32.o \
	$(OBJ)/elf64.o \
	$(OBJ)/syscall.o \
	$(OBJ)/proc.o \
	$(OBJ)/swtch.o \
	$(OBJ)/isr.o

BOOT_OBJ  := $(OBJ)/mbr.o $(OBJ)/stage2.o
HOST_PROG := $(BUILD)/mkimage

DEFAULT_GOAL := all

# Fixture images are PHONY on purpose: both are MUTATED by their
# consumers (hosttest writes KERNOS.TXT; QEMU write-gates write raw
# LBAs + the FAT file). Regenerate deterministically each time so the
# suites and boot gates never read a previous run's residue.
.PHONY: all image run clean benchdisk hosttest \
	$(BUILD)/ahcidisk.img $(BUILD)/testdisk.img

all: image

image: $(BUILD)/kernos.img

run: $(BUILD)/kernos.img
	qemu-system-x86_64 -drive format=raw,file=$(BUILD)/kernos.img,if=ide \
		-serial stdio -display none -monitor none

# -----
# Kernel link
# -----
$(BUILD)/kernel.bin: $(KERNEL_OBJ) kernel/arch/x86_64/kernel.ld
	@mkdir -p $(@D)
	$(LD) -nostdlib -T kernel/arch/x86_64/kernel.ld -o $(BUILD)/kernel.elf $(KERNEL_OBJ)
	$(OBJCOPY) -O binary $(BUILD)/kernel.elf $@

# -----
# Boot link
# -----
$(BUILD)/mbr.bin: $(OBJ)/mbr.o boot/mbr.ld
	@mkdir -p $(@D)
	$(LD) -T boot/mbr.ld -o $(BUILD)/mbr.elf $<
	$(OBJCOPY) -O binary $(BUILD)/mbr.elf $@

$(BUILD)/stage2.bin: $(OBJ)/stage2.o boot/stage2.ld
	@mkdir -p $(@D)
	$(LD) -T boot/stage2.ld -o $(BUILD)/stage2.elf $<
	$(OBJCOPY) -O binary $(BUILD)/stage2.elf $@

$(BUILD)/kernos.img: $(HOST_PROG) $(BUILD)/mbr.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin
	$< $@ $(BUILD)/mbr.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin

$(HOST_PROG): tools/mkimage.c
	@mkdir -p $(@D)
	$(CC) $(HOST_CFLAGS) -o $@ $<

# -----
# Test harness: deterministic fixture + host unit tests over the very
# same portable modules the kernel boots. mkfat32 self-checks its gear
# layout (FAT32 validity, marker collisions) before writing anything.
# -----
$(BUILD)/mkfat32: tools/mkfat32.c include/kernel/bench.h
	@mkdir -p $(@D)
	$(CC) $(HOST_CFLAGS) -Iinclude -Ikernel -o $@ $<

.PHONY: benchdisk hosttest
benchdisk: $(BUILD)/ahcidisk.img

$(BUILD)/ahcidisk.img: $(BUILD)/mkfat32 $(BUILD)/u1.elf
	$(BUILD)/mkfat32 $(BUILD)/ahcidisk.img $(BUILD)/u1.elf

$(BUILD)/testdisk.img: $(BUILD)/mkfat32 $(BUILD)/u1.elf
	$(BUILD)/mkfat32 $(BUILD)/testdisk.img $(BUILD)/u1.elf

$(BUILD)/test_fat32: tests/test_fat32.c tests/blk_host.c kernel/fat32.c \
		include/kernel/block.h kernel/fat32.h include/kernel/bench.h \
		include/kernel/types.h
	@mkdir -p $(@D)
	$(CC) $(HOST_CFLAGS) -Iinclude -Ikernel -o $@ \
		tests/test_fat32.c tests/blk_host.c kernel/fat32.c

# The ELF64 module runs on the host exactly like the kernel will: parse
# and validate, refuse everything that is not what it claims;
# test_elf64 exercises both the synthetic reject cases and the real
# U1.ELF the fixture rides.
$(BUILD)/test_elf64: tests/test_elf64.c kernel/elf64.c \
		include/kernel/elf64.h include/kernel/types.h
	@mkdir -p $(@D)
	$(CC) $(HOST_CFLAGS) -Iinclude -o $@ tests/test_elf64.c kernel/elf64.c

hosttest: $(BUILD)/testdisk.img $(BUILD)/test_fat32 $(BUILD)/test_elf64
	$(BUILD)/test_fat32 $(BUILD)/testdisk.img
	$(BUILD)/test_elf64 $(BUILD)/u1.elf

# -----
# Compilation
# -----
$(OBJ)/main.o: kernel/main.c
	@mkdir -p $(@D)
	$(CC) $(KFLAGS) -c -o $@ $<

$(OBJ)/vga.o: kernel/arch/x86_64/vga.c
	@mkdir -p $(@D)
	$(CC) $(KFLAGS) -c -o $@ $<

$(OBJ)/serial.o: kernel/arch/x86_64/serial.c
	@mkdir -p $(@D)
	$(CC) $(KFLAGS) -c -o $@ $<

$(OBJ)/cpu.o: kernel/arch/x86_64/cpu.c
	@mkdir -p $(@D)
	$(CC) $(KFLAGS) -c -o $@ $<

$(OBJ)/trap.o: kernel/arch/x86_64/trap.c
	@mkdir -p $(@D)
	$(CC) $(KFLAGS) -c -o $@ $<

$(OBJ)/idt.o: kernel/arch/x86_64/idt.c
	@mkdir -p $(@D)
	$(CC) $(KFLAGS) -c -o $@ $<

$(OBJ)/pit.o: kernel/arch/x86_64/pit.c
	@mkdir -p $(@D)
	$(CC) $(KFLAGS) -c -o $@ $<

$(OBJ)/irq.o: kernel/arch/x86_64/irq.c
	@mkdir -p $(@D)
	$(CC) $(KFLAGS) -c -o $@ $<

$(OBJ)/kbd.o: kernel/arch/x86_64/kbd.c
	@mkdir -p $(@D)
	$(CC) $(KFLAGS) -c -o $@ $<

$(OBJ)/sched.o: kernel/arch/x86_64/sched.c
	@mkdir -p $(@D)
	$(CC) $(KFLAGS) -c -o $@ $<

$(OBJ)/mem.o: kernel/arch/x86_64/mem.c
	@mkdir -p $(@D)
	$(CC) $(KFLAGS) -c -o $@ $<

$(OBJ)/pmm.o: kernel/arch/x86_64/pmm.c
	@mkdir -p $(@D)
	$(CC) $(KFLAGS) -c -o $@ $<

$(OBJ)/page.o: kernel/arch/x86_64/page.c
	@mkdir -p $(@D)
	$(CC) $(KFLAGS) -c -o $@ $<

$(OBJ)/heap.o: kernel/arch/x86_64/heap.c
	@mkdir -p $(@D)
	$(CC) $(KFLAGS) -c -o $@ $<

$(OBJ)/pci.o: kernel/arch/x86_64/pci.c
	@mkdir -p $(@D)
	$(CC) $(KFLAGS) -c -o $@ $<

$(OBJ)/ahci.o: kernel/arch/x86_64/ahci.c
	@mkdir -p $(@D)
	$(CC) $(KFLAGS) -c -o $@ $<

$(OBJ)/fat32.o: kernel/fat32.c
	@mkdir -p $(@D)
	$(CC) $(KFLAGS) -c -o $@ $<

$(OBJ)/syscall.o: kernel/arch/x86_64/syscall.c kernel/arch/x86_64/trap.h
	@mkdir -p $(@D)
	$(CC) $(KFLAGS) -c -o $@ $<

$(OBJ)/elf64.o: kernel/elf64.c include/kernel/elf64.h
	@mkdir -p $(@D)
	$(CC) $(KFLAGS) -c -o $@ $<

$(OBJ)/proc.o: kernel/arch/x86_64/proc.c kernel/arch/x86_64/proc.h
	@mkdir -p $(@D)
	$(CC) $(KFLAGS) -c -o $@ $<

# -----
# Userland (M5.3): freestanding C -> real ELF64 executable -> planted
# into the FAT fixture (mkfat32 argv). The kernel loads it from disk
# through fat_read_file + elf64_plan; nothing user-related is embedded
# in the kernel image anymore.
# -----

$(OBJ)/user1.o: kernel/user/u1.c include/kernel/syscall.h
	@mkdir -p $(@D)
	$(CC) -std=c11 -m64 -ffreestanding -fno-builtin -fno-stack-protector \
		-fno-pie -fno-pic -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
		-msoft-float -O2 -Wall -Wextra -I$(INC) -c -o $@ $<

$(BUILD)/u1.elf: $(OBJ)/user1.o kernel/user/u1.ld
	$(LD) -nostdlib -T kernel/user/u1.ld -o $@ $<

$(OBJ)/swtch.o: kernel/arch/x86_64/swtch.s
	@mkdir -p $(@D)
	$(AS) -o $@ $<

$(OBJ)/isr.o: kernel/arch/x86_64/isr.s
	@mkdir -p $(@D)
	$(AS) -o $@ $<

$(OBJ)/start64.o: kernel/arch/x86_64/start.s
	@mkdir -p $(@D)
	$(AS) -o $@ $<

$(OBJ)/mbr.o: boot/mbr.s
	@mkdir -p $(@D)
	$(AS) --32 -o $@ $<

$(OBJ)/stage2.o: boot/stage2.s
	@mkdir -p $(@D)
	$(AS) --32 -o $@ $<

# Header dependency files (KFLAGS uses -MMD -MP).
-include $(OBJ)/*.d

clean:
	rm -rf $(BUILD) $(OBJ)

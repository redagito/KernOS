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
	$(OBJ)/isr.o

BOOT_OBJ  := $(OBJ)/mbr.o $(OBJ)/stage2.o
HOST_PROG := $(BUILD)/mkimage

DEFAULT_GOAL := all
.PHONY: all image run clean

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

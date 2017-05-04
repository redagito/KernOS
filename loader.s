.set MAGIC, 0x1badb002
.set FLAGS, (1 << 0 | 1 << 1)
.set CHECKSUM, -(MAGIC + FLAGS)

# Multiboot section for grub
.section .multiboot
    .long MAGIC
    .long FLAGS
    .long CHECKSUM

.section .text

# External kernel main entry point
.extern kernel_main
# External constructor for global objects
.extern call_constructors

# Global entry
.global loader

loader:
    # Set stack pointer
    mov $kernel_stack, %esp

    # Call all constructors for global objects
    call call_constructors

    push %eax
    push %ebx
    call kernel_main

_stop:
    cli
    hlt
    jmp _stop

.section .bss

# 2 MB
.space 2 * 1024 * 1024

kernel_stack:

.DEFAULT_GOAL := runkernel

# C++ flags
CXXFLAGS = -m32 -ffreestanding -fno-use-cxa-atexit -nostdlib -fno-builtin -fno-rtti -fno-exceptions -fno-leading-underscore -std=c++11 -Wall -Wextra
ASFLAGS = --32
LDFLAGS = -melf_i386

objects = obj/loader.o obj/kernel.o obj/vga.o obj/string.o obj/print.o obj/gdt.o

# C++ sources
obj/%.o: %.cpp
	g++ $(CXXFLAGS) -o $@ -c $<

# ASM sources
obj/%.o: %.s
	as $(ASFLAGS) -o $@ $<

bin/kernel.bin: linker.ld $(objects)
	ld $(LDFLAGS) -T $< -o $@ $(objects)
	./script/verify_kernel.sh

# Currently does not work
bin/kernel.iso: bin/kernel.bin
	mkdir bin/iso
	mkdir bin/iso/boot
	mkdir bin/iso/boot/grub
	cp $< bin/iso/boot/
	echo 'menuentry "KernOS" {' >> bin/iso/boot/grub/grub.cfg
	echo '    multiboot /boot/kernel.bin' >> bin/iso/boot/grub/grub.cfg
	echo '    boot' >> bin/iso/boot/grub/grub.cfg
	echo '}' >> bin/iso/boot/grub/grub.cfg
	grub-mkrescue -o $@ bin/iso
	rm -rf bin/iso

runiso: bin/kernel.iso
	qemu-system-i386 -cdrom $<

# Default target, works best
runkernel: bin/kernel.bin
	qemu-system-i386 -kernel $<

clean:
	rm $(objects) -f
	rm bin/kernel.bin -f
	rm bin/kernel.iso -f
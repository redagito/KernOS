# C++ flags
CXXFLAGS = -m32 -fno-use-cxa-atexit -nostdlib -fno-builtin -fno-rtti -fno-exceptions -fno-leading-underscore
ASFLAGS = --32
LDFLAGS = -melf_i386

objects = loader.o kernel.o

# C++ sources
%.o: %.cpp
	g++ $(CXXFLAGS) -o $@ -c $<

# ASM sources
%.o: %.s
	as $(ASFLAGS) -o $@ $<

kernel.bin: linker.ld $(objects)
	ld $(LDFLAGS) -T $< -o $@ $(objects)
	./script/verify_kernel.sh

kernel.iso: kernel.bin
	mkdir iso
	mkdir iso/boot
	mkdir iso/boot/grub
	cp $< iso/boot/
	echo 'menuentry "KernOS" {' >> iso/boot/grub/grub.cfg
	echo '    multiboot /boot/kernel.bin' >> iso/boot/grub/grub.cfg
	echo '    boot' >> iso/boot/grub/grub.cfg
	echo '}' >> iso/boot/grub/grub.cfg
	grub-mkrescue -o $@ iso
	rm -rf iso

runiso: kernel.iso
	qemu-system-i386 -cdrom $<

runkernel: kernel.bin
	qemu-system-i386 -kernel $<

clean:
	rm $(objects)
	rm kernel.bin
	rm kernel.iso
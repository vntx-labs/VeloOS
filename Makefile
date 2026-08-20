CC = gcc
ARCH = x86_64

EFI_INCLUDE = /usr/include/efi
EFI_INC_ARCH = /usr/include/efi/$(ARCH)

CFLAGS = -I$(EFI_INCLUDE) -I$(EFI_INC_ARCH) -DGNU_EFI_USE_MS_ABI \
         -fno-stack-protector -fpic -fshort-wchar -mno-red-zone -Wall -Wextra

# ahci.o hinzugefügt
OBJS = kernel.o font.o keyboard.o ahci.o fat32.o

LDFLAGS = -nostdlib -znocombreloc -shared -Bsymbolic \
          -T /usr/lib/elf_x86_64_efi.lds \
          /usr/lib/crt0-efi-$(ARCH).o $(OBJS) \
          -L/usr/lib -lefi -lgnuefi

all: os.img

kernel.o: kernel.c font.h keyboard.h ahci.h
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

font.o: font.c font.h
	$(CC) $(CFLAGS) -c font.c -o font.o

keyboard.o: keyboard.c keyboard.h
	$(CC) $(CFLAGS) -c keyboard.c -o keyboard.o


# Neue Regel für ahci.o
ahci.o: ahci.c ahci.h
	$(CC) $(CFLAGS) -c ahci.c -o ahci.o

fat32.o: fat32.c fat32.h
	$(CC) $(CFLAGS) -c fat32.c -o fat32.o

kernel.efi: $(OBJS)
	ld $(LDFLAGS) -o kernel.so
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel \
            -j .reloc -j .eh_frame --target efi-app-$(ARCH) kernel.so kernel.efi

os.img: kernel.efi
	dd if=/dev/zero of=os.img bs=1M count=64
	mkfs.vfat -F 32 os.img
	mmd -i os.img ::/EFI
	mmd -i os.img ::/EFI/BOOT
	mcopy -i os.img kernel.efi ::/EFI/BOOT/BOOTX64.EFI

run: os.img
	qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
                       -drive format=raw,file=os.img \
                       -machine q35 \
                       -device qemu-xhci,id=xhci \
                       -device usb-tablet,bus=xhci.0 \
                       -serial file:qemu.log

clean:
	rm -f *.o *.so *.efi *.img
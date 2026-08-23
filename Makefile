CC = gcc
LD = ld
OBJCOPY = objcopy

ARCH = x86_64

EFI_INCLUDE = /usr/include/efi
EFI_INC_ARCH = /usr/include/efi/$(ARCH)

CFLAGS = -I$(EFI_INCLUDE) \
         -I$(EFI_INC_ARCH) \
         -DGNU_EFI_USE_MS_ABI \
         -fno-stack-protector \
         -fpic \
         -fshort-wchar \
         -mno-red-zone \
         -Wall \
         -Wextra

# ------------------------------------------------------------
# Object files
# ------------------------------------------------------------

OBJS = kernel.o \
       font.o \
       keyboard.o \
       ahci.o \
       fat32.o \
       wm.o \
       desktop.o

# ------------------------------------------------------------
# EFI linker flags
# ------------------------------------------------------------

LDFLAGS = -nostdlib \
          -znocombreloc \
          -shared \
          -Bsymbolic \
          -T /usr/lib/elf_x86_64_efi.lds \
          /usr/lib/crt0-efi-$(ARCH).o \
          $(OBJS) \
          -L/usr/lib \
          -lefi \
          -lgnuefi

.PHONY: all clean rebuild run

# ------------------------------------------------------------
# Default target
# ------------------------------------------------------------

all: os.img

# ------------------------------------------------------------
# Kernel & Modules
# ------------------------------------------------------------

kernel.o: kernel.c font.h keyboard.h ahci.h fat32.h desktop.h wm.h
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

font.o: font.c font.h
	$(CC) $(CFLAGS) -c font.c -o font.o

keyboard.o: keyboard.c keyboard.h
	$(CC) $(CFLAGS) -c keyboard.c -o keyboard.o

ahci.o: ahci.c ahci.h
	$(CC) $(CFLAGS) -c ahci.c -o ahci.o

fat32.o: fat32.c fat32.h ahci.h
	$(CC) $(CFLAGS) -c fat32.c -o fat32.o

wm.o: wm.c wm.h font.h
	$(CC) $(CFLAGS) -c wm.c -o wm.o

desktop.o: desktop.c desktop.h font.h wm.h
	$(CC) $(CFLAGS) -c desktop.c -o desktop.o

# ------------------------------------------------------------
# EFI Kernel
# ------------------------------------------------------------

kernel.efi: $(OBJS)
	$(LD) $(LDFLAGS) -o kernel.so

	$(OBJCOPY) \
		-j .text \
		-j .sdata \
		-j .data \
		-j .dynamic \
		-j .dynsym \
		-j .rel \
		-j .reloc \
		-j .eh_frame \
		--target=efi-app-$(ARCH) \
		kernel.so \
		kernel.efi

# ------------------------------------------------------------
# Disk Images
# ------------------------------------------------------------

os.img: kernel.efi
	rm -f os.img
	dd if=/dev/zero of=os.img bs=1M count=64
	mkfs.vfat -F 32 os.img
	mmd -i os.img ::/EFI
	mmd -i os.img ::/EFI/BOOT
	mcopy -i os.img kernel.efi ::/EFI/BOOT/BOOTX64.EFI

# Erstellt data.img NUR, wenn die Datei physisch nicht existiert
data.img:
	dd if=/dev/zero of=data.img bs=1M count=1024

# ------------------------------------------------------------
# Run mit zwei Laufwerken
# ------------------------------------------------------------

run: os.img data.img
	qemu-system-x86_64 \
		-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
		-machine q35 \
		-drive file=os.img,format=raw,if=none,id=bootdisk \
		-device ide-hd,drive=bootdisk,bus=ide.0 \
		-drive file=data.img,format=raw,if=none,id=datadisk \
		-device ide-hd,drive=datadisk,bus=ide.1 \
		-device qemu-xhci,id=xhci \
		-device usb-tablet,bus=xhci.0 \
		-serial stdio

# ------------------------------------------------------------
# Clean & Rebuild (data.img wird hier BESCHÜTZT)
# ------------------------------------------------------------

clean:
	rm -f *.o *.so *.efi os.img qemu.log

# Falls du die Festplatte DOCH mal komplett plattmachen willst
clean-all: clean
	rm -f data.img

rebuild: clean
	$(MAKE) all

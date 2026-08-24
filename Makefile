CC = gcc
LD = ld
OBJCOPY = objcopy
ARCH = x86_64

EFI_INCLUDE = /usr/include/efi
EFI_INC_ARCH = /usr/include/efi/$(ARCH)

CFLAGS = -I$(EFI_INCLUDE) \
         -I$(EFI_INC_ARCH) \
         -fno-stack-protector \
         -fpic \
         -fshort-wchar \
         -mno-red-zone \
         -march=x86-64 \
         -mno-avx \
         -mno-avx2 \
         -mno-sse3 \
         -mno-ssse3 \
         -mno-sse4.1 \
         -mno-sse4.2 \
         -Wall \
         -Wextra \
         -O2

OBJS = kernel.o \
       font.o \
       wm.o \
       ahci.o \
       fat32.o \
       keyboard.o \
       mouse.o \
       net.o \
       desktop.o \
       setup.o \
       syscall.o

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

.PHONY: all clean run reset-disk

all: os.img

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel.efi: $(OBJS)
	$(LD) $(LDFLAGS) -o kernel.so
	$(OBJCOPY) \
		-j .text \
		-j .sdata \
		-j .data \
		-j .dynamic \
		-j .dynsym \
		-j .rel \
		-j .rela \
		-j .reloc \
		--target=efi-app-$(ARCH) \
		kernel.so \
		kernel.efi

os.img: kernel.efi
	@if [ ! -f os.img ]; then \
		echo "[+] Erstelle neues 64 MB Datentraeger-Image..."; \
		dd if=/dev/zero of=os.img bs=1M count=64 status=none; \
		mkfs.vfat -F 32 os.img > /dev/null; \
		mmd -i os.img ::/EFI; \
		mmd -i os.img ::/EFI/BOOT; \
	fi
	@echo "[+] Aktualisiere BOOTX64.EFI auf bestehender Disk..."
	@mcopy -o -i os.img kernel.efi ::/EFI/BOOT/BOOTX64.EFI
	@if [ -f WALL.BIN ]; then mcopy -o -i os.img WALL.BIN ::/WALL.BIN; fi

run: os.img
	qemu-system-x86_64 \
		-cpu max \
		-m 4056M \
		-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
		-machine q35 \
		-drive file=os.img,format=raw,if=none,id=bootdisk \
		-device ide-hd,drive=bootdisk,bus=ide.0 \
		-serial stdio

reset-disk:
	rm -f os.img
	@echo "[+] Festplattenabbild zurueckgesetzt."

clean:
	rm -f *.o *.so *.efi
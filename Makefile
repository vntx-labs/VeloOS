CC = gcc
LD = ld
OBJCOPY = objcopy
AR = ar
ARCH = x86_64

# ==========================================
# VERZEICHNISSE & HEADER
# ==========================================
EFI_INCLUDE   = /usr/include/efi
EFI_INC_ARCH  = /usr/include/efi/$(ARCH)
USER_INCLUDE  = -I./include

# ==========================================
# KERNEL COMPILER-FLAGS (FREESTANDING / EFI)
# ==========================================
CFLAGS = -I$(EFI_INCLUDE) \
         -I$(EFI_INC_ARCH) \
         -fno-stack-protector \
         -fpic \
         -fshort-wchar \
         -mno-red-zone \
         -march=x86-64 \
         -mno-sse \
         -mno-sse2 \
         -mno-mmx \
         -mno-avx \
         -mno-avx2 \
         -mno-sse3 \
         -mno-ssse3 \
         -mno-sse4.1 \
         -mno-sse4.2 \
         -fno-tree-loop-distribute-patterns \
         -Wall \
         -Wextra \
         -O2

# ==========================================
# USERLAND APP COMPILER-FLAGS (RING 3 LIBC)
# ==========================================
USER_CFLAGS = $(USER_INCLUDE) \
              -fno-stack-protector \
              -fPIE \
              -mno-red-zone \
              -march=x86-64 \
              -mno-sse \
              -mno-sse2 \
              -mno-mmx \
              -Wall \
              -Wextra \
              -O2

USER_LDFLAGS = -nostdlib -pie

# ==========================================
# OBJEKTDATEIEN
# ==========================================
KERNEL_OBJS = kernel.o \
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
          $(KERNEL_OBJS) \
          -L/usr/lib \
          -lefi \
          -lgnuefi

.PHONY: all clean run reset-disk apps libc

all: os.img

# ==========================================
# KERNEL KOMPILIERUNG
# ==========================================
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel.efi: $(KERNEL_OBJS)
	$(LD) $(LDFLAGS) -o kernel.so
	$(OBJCOPY) \
		-j .text \
		-j .sdata \
		-j .data \
		-j .rodata \
		-j .dynamic \
		-j .dynsym \
		-j .rel \
		-j .rela \
		-j .reloc \
		--target=efi-app-$(ARCH) \
		kernel.so \
		kernel.efi

# ==========================================
# VELOLIBC & USERLAND APPS BAUEN
# ==========================================
libc: libc/velolibc.o
	@mkdir -p lib
	$(AR) rcs lib/libvelo.a libc/velolibc.o

libc/velolibc.o: libc/velolibc.c
	@mkdir -p libc
	$(CC) $(USER_CFLAGS) -c libc/velolibc.c -o libc/velolibc.o

apps: libc
	@if [ -d apps ]; then \
		for app in apps/*.c; do \
			if [ -f "$$app" ]; then \
				name=$$(basename "$$app" .c | tr 'a-z' 'A-Z'); \
				echo "[+] Kompiliere Userland App: $$app -> $$name.BIN"; \
				$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) "$$app" libc/velolibc.c -o "$$name.BIN"; \
			fi; \
		done; \
	fi

# ==========================================
# DISK-IMAGE ERSTELLUNG
# ==========================================
os.img: kernel.efi apps
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
	@for bin in *.BIN; do \
		if [ -f "$$bin" ] && [ "$$bin" != "WALL.BIN" ]; then \
			echo "[+] Installiere App auf FAT32 Disk: $$bin"; \
			mcopy -o -i os.img "$$bin" "::/$$bin"; \
		fi; \
	done

# ==========================================
# EMULATION
# ==========================================
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
	rm -f *.o *.so *.efi *.BIN libc/*.o lib/*.a
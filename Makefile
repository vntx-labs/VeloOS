CC = gcc
LD = ld
OBJCOPY = objcopy
STRIP = strip
AR = ar
ARCH = x86_64

# ==========================================
# VERZEICHNISSE & HEADER
# ==========================================
EFI_INCLUDE   = /usr/include/efi
EFI_INC_ARCH  = /usr/include/efi/$(ARCH)
USER_INCLUDE  = -I./include -I./include/libc

# ==========================================
# KERNEL COMPILER-FLAGS (FREESTANDING / EFI)
# ==========================================
CFLAGS = -I$(EFI_INCLUDE) \
         -I$(EFI_INC_ARCH) \
         -I./include \
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
         -fno-builtin \
         -fno-tree-loop-distribute-patterns \
         -Wall \
         -Wextra \
         -O2

# ==========================================
# USERLAND C APP COMPILER-FLAGS (VELO LIBC)
# ==========================================
USER_CFLAGS = $(USER_INCLUDE) \
              -fno-stack-protector \
              -fPIE \
              -fno-builtin \
              -mno-red-zone \
              -march=x86-64 \
              -Wall \
              -Wextra \
              -O2

USER_LDFLAGS = -nostartfiles -pie -Wl,-Bsymbolic -Wl,-z,max-page-size=4096 -Wl,-z,common-page-size=4096 -Wl,-e,_start

# ==========================================
# OBJEKTDATEIEN KERNEL
# ==========================================
KERNEL_OBJS = kernel.o \
              font.o \
              wm.o \
              ahci.o \
              nvme.o \
              fat32.o \
              keyboard.o \
              mouse.o \
              net.o \
              desktop.o \
              setup.o \
              syscall.o \
              sched.o \
              pmm_vmm.o

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

.PHONY: all clean run reset-disk disk-reset apps libc baremetal dist flash iso ventoy

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
		for dir in apps/*; do \
			if [ -d "$$dir" ]; then \
				name=$$(basename "$$dir" | tr 'a-z' 'A-Z'); \
				echo "[+] Kompiliere C Userland App: $$dir -> $$name.BIN"; \
				$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -nostdlib libc/velolibc.o $$dir/*.c -o "$$name.BIN"; \
				$(STRIP) --strip-debug "$$name.BIN"; \
			fi; \
		done; \
	fi

# ==========================================
# DISK-IMAGE UPDATE (512 MB) - IN-PLACE
# ==========================================
os.img: kernel.efi apps
	@if [ ! -f os.img ]; then \
		echo "[+] Initialisiere neues 512 MB UEFI-Image (os.img)..."; \
		rm -f esp.img; \
		dd if=/dev/zero of=esp.img bs=1M count=512 status=none; \
		mkfs.vfat -F 32 -n "VELO_BOOT" esp.img > /dev/null; \
		dd if=/dev/zero of=os.img bs=1M count=513 status=none; \
		printf "label: dos\nstart=2048, size=1048576, type=ef, bootable\n" | sfdisk os.img > /dev/null 2>&1; \
		dd if=esp.img of=os.img bs=512 seek=2048 conv=notrunc status=none; \
		rm -f esp.img; \
	fi
	@echo "[+] Aktualisiere Bootloader & Apps..."
	@mmd -D s -i os.img@@1M ::/EFI 2>/dev/null || true
	@mmd -D s -i os.img@@1M ::/EFI/BOOT 2>/dev/null || true
	@mdel -i os.img@@1M ::/EFI/BOOT/BOOTX64.EFI 2>/dev/null || true
	@mcopy -i os.img@@1M kernel.efi ::/EFI/BOOT/BOOTX64.EFI
	@if [ -f WALL.BIN ]; then \
		mdel -i os.img@@1M ::/WALL.BIN 2>/dev/null || true; \
		mcopy -i os.img@@1M WALL.BIN ::/WALL.BIN; \
	fi
	@for bin in *.BIN; do \
		if [ -f "$$bin" ] && [ "$$bin" != "WALL.BIN" ]; then \
			mdel -i os.img@@1M "::/$$bin" 2>/dev/null || true; \
			mcopy -i os.img@@1M "$$bin" "::/$$bin"; \
		fi; \
	done

# ==========================================
# VENTOY & BARE-METAL
# ==========================================
ventoy: os.img
	@cp os.img velo.img
	@echo "[+] 'velo.img' erfolgreich fuer Ventoy aktualisiert!"

iso: ventoy
	@if command -v xorriso >/dev/null 2>&1; then \
		echo "[+] Erstelle velo.iso..."; \
		rm -rf iso_root efiboot.img velo.iso; \
		mkdir -p iso_root/EFI/BOOT; \
		cp kernel.efi iso_root/EFI/BOOT/BOOTX64.EFI; \
		for bin in *.BIN; do if [ -f "$$bin" ]; then cp "$$bin" iso_root/; fi; done; \
		dd if=/dev/zero of=efiboot.img bs=1M count=512 status=none; \
		mkfs.vfat -F 32 -n "VELO_EFI" efiboot.img > /dev/null; \
		mmd -D s -i efiboot.img ::/EFI 2>/dev/null || true; \
		mmd -D s -i efiboot.img ::/EFI/BOOT 2>/dev/null || true; \
		mcopy -o -i efiboot.img kernel.efi ::/EFI/BOOT/BOOTX64.EFI; \
		for bin in *.BIN; do if [ -f "$$bin" ]; then mcopy -o -i efiboot.img "$$bin" "::/$$bin"; fi; done; \
		cp efiboot.img iso_root/; \
		xorriso -as mkisofs -iso-level 3 -full-iso9660-filenames -volid "VELO_OS" -eltorito-alt-boot -e efiboot.img -no-emul-boot -isohybrid-gpt-basdat -o velo.iso iso_root >/dev/null 2>&1; \
		rm -rf iso_root efiboot.img; \
		echo "[+] 'velo.iso' erfolgreich erstellt!"; \
	fi

baremetal: ventoy

dist: kernel.efi apps
	@echo "[+] Erstelle einsatzbereiten 'dist/' Ordner..."
	@rm -rf dist
	@mkdir -p dist/EFI/BOOT
	@cp kernel.efi dist/EFI/BOOT/BOOTX64.EFI
	@if [ -f WALL.BIN ]; then cp WALL.BIN dist/; fi
	@for bin in *.BIN; do \
		if [ -f "$$bin" ]; then cp "$$bin" dist/; fi; \
	done

flash: os.img
	@if [ -z "$(TARGET)" ]; then \
		echo "[-] Fehler: Kein Ziel angegeben! Syntax: make flash TARGET=/dev/sdX"; \
		exit 1; \
	fi
	@sudo dd if=os.img of=$(TARGET) bs=4M status=progress conv=fsync

# ==========================================
# EMULATION (SATA AHCI + PCIe NVMe SSD)
# ==========================================
run: os.img
	@if [ ! -f nvmedisk.img ]; then \
		echo "[+] Erstelle 2 GB PCIe NVMe SSD (nvmedisk.img)..."; \
		dd if=/dev/zero of=nvmedisk.img bs=1M count=2048 status=none; \
	fi
	qemu-system-x86_64 \
		-cpu host \
		-cpu max \
		-m 4096M \
		-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
		-machine q35 \
		-drive file=os.img,format=raw,if=none,id=bootdisk \
		-device ide-hd,drive=bootdisk,bus=ide.0 \
		-netdev user,id=net0 \
		-device e1000,netdev=net0 \
		-enable-kvm \
		-serial stdio

# ==========================================
# DISK RESET
# ==========================================
disk-reset: reset-disk
reset-disk:
	rm -f os.img velo.img esp.img velo.iso nvmedisk.img efiboot.img
	@echo "[+] Alle Festplattenabbilder wurden vollstaendig zurueckgesetzt."

# ==========================================
# MAKE CLEAN
# ==========================================
clean:
	rm -f *.o *.so *.efi *.BIN libc/*.o lib/*.a esp.img efiboot.img
	rm -rf dist iso_root /tmp/litehtml_build
	@echo "[+] Build-Dateien aufgeraeumt (Festplatten-Images os.img und nvmedisk.img bleiben erhalten)."
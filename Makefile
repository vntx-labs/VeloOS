CC      = gcc
LD      = ld
OBJCOPY = objcopy
STRIP   = strip
AR      = ar
ARCH    = x86_64

# ==========================================
# VERZEICHNISSE & HEADER
# ==========================================
EFI_INCLUDE  = /usr/include/efi
EFI_INC_ARCH = /usr/include/efi/$(ARCH)

# Header fuer Ring-3 Velo-Apps (nutzen deine VeloLIBC)
USER_INCLUDE = -I./include -I./include/libc -I./include/velo

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
# USERLAND APP COMPILER-FLAGS (REINES C)
# ==========================================
USER_CFLAGS = $(USER_INCLUDE) \
              -fno-stack-protector \
              -fPIE \
              -fno-builtin \
              -mno-red-zone \
              -march=x86-64 \
              -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
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
              pmm_vmm.o \
              kshell.o \
              wallpaper.o

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

.PHONY: all clean run reset-disk disk-reset apps bin_tools libc iso

all: os.img

# ==========================================
# WALLPAPER CONVERSION & LINKING
# ==========================================
wallpaper.bin:
	@if [ -f wallpaper.png ]; then \
		echo "[+] Konvertiere wallpaper.png zu raw BGRA..."; \
		python3 -c "from PIL import Image; img=Image.open('wallpaper.png').convert('RGBA'); r,g,b,a=img.split(); Image.merge('RGBA',(b,g,r,a)).save('wallpaper.bin')" 2>/dev/null || \
		ffmpeg -y -i wallpaper.png -vcodec rawvideo -pix_fmt bgra wallpaper.bin >/dev/null 2>&1 || \
		convert wallpaper.png -depth 8 bgra:wallpaper.bin >/dev/null 2>&1 || \
		dd if=/dev/zero of=wallpaper.bin bs=1024 count=3072 status=none; \
	else \
		dd if=/dev/zero of=wallpaper.bin bs=1024 count=3072 status=none; \
	fi

wallpaper.o: wallpaper.bin
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --redefine-sym _binary_wallpaper_bin_start=g_wallpaper_start \
	  --redefine-sym _binary_wallpaper_bin_end=g_wallpaper_end \
	  --redefine-sym _binary_wallpaper_bin_size=g_wallpaper_size \
	  wallpaper.bin wallpaper.o

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
# VELOLIBC BAUEN
# ==========================================
libc: libc/velolibc.o
	@mkdir -p lib
	$(AR) rcs lib/libvelo.a libc/velolibc.o

libc/velolibc.o: libc/velolibc.c
	@mkdir -p libc
	$(CC) $(USER_CFLAGS) -c libc/velolibc.c -o libc/velolibc.o

# ==========================================
# USERLAND APPS (apps/*) BAUEN
# ==========================================
apps: libc
	@set -e; \
	if [ -d apps ]; then \
		for dir in apps/*; do \
			if [ -d "$$dir" ]; then \
				name=$$(basename "$$dir" | tr 'a-z' 'A-Z'); \
				c_srcs=$$(find "$$dir" -type f -name "*.c" | tr '\n' ' '); \
				if [ -n "$$c_srcs" ]; then \
					echo "[+] Kompiliere GUI App: $$dir -> $$name.BIN"; \
					obj_files=""; \
					for cs in $$c_srcs; do \
						obj=$${cs%.c}.o; \
						$(CC) $(USER_CFLAGS) -c "$$cs" -o "$$obj"; \
						obj_files="$$obj_files $$obj"; \
					done; \
					$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -nostdlib libc/velolibc.o $$obj_files -o "$$name.BIN"; \
					$(STRIP) --strip-debug "$$name.BIN"; \
					rm -f $$obj_files; \
				fi; \
			fi; \
		done; \
	fi

# ==========================================
# UNIX COREUTILS (bin/*.c) BAUEN
# ==========================================
bin_tools: libc
	@set -e; \
	if [ -d bin ]; then \
		mkdir -p bin_out; \
		for f in bin/*.c; do \
			if [ -f "$$f" ]; then \
				cmd=$$(basename "$$f" .c | tr 'a-z' 'A-Z'); \
				echo "[+] Kompiliere Unix Binary: $$f -> bin_out/$$cmd.BIN"; \
				$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -nostdlib libc/velolibc.o "$$f" -o "bin_out/$$cmd.BIN"; \
				$(STRIP) --strip-debug "bin_out/$$cmd.BIN"; \
			fi; \
		done; \
	fi

# ==========================================
# DISK-IMAGE: BARE-METAL & UEFI KOMPATIBEL
# ==========================================
os.img: kernel.efi apps bin_tools
	@if [ ! -f os.img ]; then \
		echo "[+] Initialisiere 514 MB Festplatten-Image (os.img) mit MBR-ESP..."; \
		rm -f esp.img; \
		dd if=/dev/zero of=esp.img bs=1M count=512 status=none; \
		mkfs.vfat -F 32 -n "VELO_BOOT" esp.img > /dev/null; \
		dd if=/dev/zero of=os.img bs=1M count=514 status=none; \
		printf "label: dos\nstart=2048, size=1048576, type=ef, bootable\n" | sfdisk os.img > /dev/null 2>&1; \
		dd if=esp.img of=os.img bs=512 seek=2048 conv=notrunc status=none; \
		rm -f esp.img; \
	fi
	@echo "[+] Erstelle Verzeichnis-Hierarchie auf os.img..."
	@mmd -D s -i os.img@@1M ::/EFI 2>/dev/null || true
	@mmd -D s -i os.img@@1M ::/EFI/BOOT 2>/dev/null || true
	@mmd -D s -i os.img@@1M ::/bin 2>/dev/null || true
	@mmd -D s -i os.img@@1M ::/apps 2>/dev/null || true
	@mmd -D s -i os.img@@1M ::/Programs 2>/dev/null || true
	@mmd -D s -i os.img@@1M ::/Program\ Files 2>/dev/null || true
	@mmd -D s -i os.img@@1M ::/VeloOS 2>/dev/null || true
	@mmd -D s -i os.img@@1M ::/VeloOS/System32 2>/dev/null || true
	@mmd -D s -i os.img@@1M ::/Users 2>/dev/null || true
	@mmd -D s -i os.img@@1M ::/Users/Desktop 2>/dev/null || true
	@mmd -D s -i os.img@@1M ::/Users/Documents 2>/dev/null || true
	@mmd -D s -i os.img@@1M ::/Users/Downloads 2>/dev/null || true
	@mmd -D s -i os.img@@1M ::/Users/Pictures 2>/dev/null || true
	@echo "[+] Kopiere System-Dateien & Boot-Loader..."
	@mdel -i os.img@@1M ::/EFI/BOOT/BOOTX64.EFI 2>/dev/null || true
	@mcopy -i os.img@@1M kernel.efi ::/EFI/BOOT/BOOTX64.EFI
	@if [ -f WALL.BIN ]; then mcopy -o -i os.img@@1M WALL.BIN ::/VeloOS/WALL.BIN; fi
	@if [ -d bin_out ]; then \
		for b in bin_out/*.BIN; do \
			if [ -f "$$b" ]; then \
				fname=$$(basename "$$b"); \
				mcopy -o -i os.img@@1M "$$b" "::/bin/$$fname"; \
			fi; \
		done; \
	fi
	@for app in *.BIN; do \
		if [ -f "$$app" ] && [ "$$app" != "WALL.BIN" ]; then \
			mcopy -o -i os.img@@1M "$$app" "::/$$app"; \
			mcopy -o -i os.img@@1M "$$app" "::/bin/$$app"; \
			mcopy -o -i os.img@@1M "$$app" "::/Programs/$$app"; \
		fi; \
	done
	@printf "Willkommen bei VeloOS!\n" > /tmp/velo_readme.txt
	@mcopy -o -i os.img@@1M /tmp/velo_readme.txt "::/Users/Documents/README.TXT"
	@printf "Willkommen auf dem VeloOS Desktop!\n" > /tmp/velo_welcome.txt
	@mcopy -o -i os.img@@1M /tmp/velo_welcome.txt "::/Users/Desktop/Willkommen.txt"
	@rm -f /tmp/velo_readme.txt /tmp/velo_welcome.txt
	@if [ -f nvmedisk.img ]; then \
		echo "[+] Synchronisiere Binaries mit nvmedisk.img..."; \
		mdel -i nvmedisk.img@@1M ::/EFI/BOOT/BOOTX64.EFI 2>/dev/null || true; \
		mcopy -o -i nvmedisk.img@@1M kernel.efi ::/EFI/BOOT/BOOTX64.EFI 2>/dev/null || true; \
		for app in *.BIN; do \
			if [ -f "$$app" ] && [ "$$app" != "WALL.BIN" ]; then \
				mcopy -o -i nvmedisk.img@@1M "$$app" "::/$$app" 2>/dev/null || true; \
				mcopy -o -i nvmedisk.img@@1M "$$app" "::/bin/$$app" 2>/dev/null || true; \
				mcopy -o -i nvmedisk.img@@1M "$$app" "::/Programs/$$app" 2>/dev/null || true; \
			fi; \
		done; \
		if [ -d bin_out ]; then \
			for b in bin_out/*.BIN; do \
				if [ -f "$$b" ]; then \
					fname=$$(basename "$$b"); \
					mcopy -o -i nvmedisk.img@@1M "$$b" "::/bin/$$fname" 2>/dev/null || true; \
				fi; \
			done; \
		fi; \
	fi

# ==========================================
# ISO-BUILD TARGET (UEFI FAT32 ESP + VENTOY)
# ==========================================
iso: kernel.efi apps bin_tools
	@rm -rf iso_root efiboot.img velo.iso
	@mkdir -p iso_root/EFI/BOOT
	@echo "[+] Erstelle 96 MB FAT32 EFI-System-Partition..."
	@dd if=/dev/zero of=efiboot.img bs=1M count=96 status=none
	@mkfs.vfat -F 32 -n "VELO_BOOT" efiboot.img > /dev/null
	@mmd -i efiboot.img ::/EFI 2>/dev/null || true
	@mmd -i efiboot.img ::/EFI/BOOT 2>/dev/null || true
	@mmd -i efiboot.img ::/bin 2>/dev/null || true
	@mmd -i efiboot.img ::/apps 2>/dev/null || true
	@mmd -i efiboot.img ::/Programs 2>/dev/null || true
	@mmd -i efiboot.img ::/VeloOS 2>/dev/null || true
	@mmd -i efiboot.img ::/VeloOS/System32 2>/dev/null || true
	@mmd -i efiboot.img ::/Users 2>/dev/null || true
	@mmd -i efiboot.img ::/Users/Desktop 2>/dev/null || true
	@mmd -i efiboot.img ::/Users/Documents 2>/dev/null || true
	@mcopy -i efiboot.img kernel.efi ::/EFI/BOOT/BOOTX64.EFI
	@if [ -f WALL.BIN ]; then mcopy -o -i efiboot.img WALL.BIN ::/VeloOS/WALL.BIN; fi
	@if [ -d bin_out ]; then \
		for b in bin_out/*.BIN; do \
			if [ -f "$$b" ]; then \
				fname=$$(basename "$$b"); \
				mcopy -o -i efiboot.img "$$b" "::/bin/$$fname"; \
			fi; \
		done; \
	fi
	@for app in *.BIN; do \
		if [ -f "$$app" ] && [ "$$app" != "WALL.BIN" ]; then \
			mcopy -o -i efiboot.img "$$app" "::/$$app"; \
			mcopy -o -i efiboot.img "$$app" "::/bin/$$app"; \
			mcopy -o -i efiboot.img "$$app" "::/Programs/$$app"; \
		fi; \
	done
	@printf "VeloOS Live USB\n" > /tmp/live.txt
	@mcopy -o -i efiboot.img /tmp/live.txt "::/Users/Desktop/Willkommen.txt"
	@rm -f /tmp/live.txt
	@cp efiboot.img iso_root/EFI/BOOT/efiboot.img
	@cp kernel.efi iso_root/EFI/BOOT/BOOTX64.EFI
	@echo "[+] Generiere hybrid-bootfaehiges ISO mit El-Torito UEFI (0xEF) & GPT-Partition..."
	@xorriso -as mkisofs \
		-iso-level 3 \
		-full-iso9660-filenames \
		-R -r -J -joliet-long \
		-V "VELO_BOOT" \
		-eltorito-alt-boot \
		-e EFI/BOOT/efiboot.img \
		-no-emul-boot \
		-isohybrid-gpt-basdat \
		-o velo.iso iso_root
	@rm -rf iso_root efiboot.img
	@echo ""
	@echo "========================================================="
	@echo "[ERFOLG] velo.iso wurde erfolgreich gebaut!"
	@echo "Kompatibel mit: Ventoy, Rufus (DD-Modus) & direktem USB-Boot"
	@echo "========================================================="

# ==========================================
# EMULATION (QEMU MIT ECHTEM USB 3.0 xHCI + TABLET + TASTATUR)
# ==========================================
run: os.img
	@if [ ! -f nvmedisk.img ]; then \
		echo "[+] Erstelle 2 GB PCIe NVMe SSD (nvmedisk.img)..."; \
		dd if=/dev/zero of=nvmedisk.img bs=1M count=2048 status=none; \
	fi
	qemu-system-x86_64 \
	  -cpu host \
	  -m 4096M \
	  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
	  -machine q35 \
	  -device qemu-xhci,id=xhci \
	  -device usb-tablet,bus=xhci.0 \
	  -drive file=os.img,format=raw,if=none,id=bootdisk \
	  -device ide-hd,drive=bootdisk,bus=ide.0 \
	  -drive file=nvmedisk.img,format=raw,if=none,id=nvme0 \
	  -device nvme,drive=nvme0,serial=nvme-1 \
	  -netdev user,id=net0 \
	  -device e1000,netdev=net0 \
	  -enable-kvm \
	  -serial stdio

disk-reset: reset-disk
reset-disk:
	rm -f os.img velo.img esp.img velo.iso nvmedisk.img efiboot.img wallpaper.bin wallpaper.o
	@echo "[+] Alle Festplattenabbilder wurden vollstaendig zurueckgesetzt."

clean:
	rm -f *.o *.so *.efi *.BIN libc/*.o lib/*.a esp.img efiboot.img velo.iso wallpaper.bin wallpaper.o
	rm -rf dist iso_root bin_out /tmp/litehtml_build
	@echo "[+] Build-Dateien aufgeraeumt."
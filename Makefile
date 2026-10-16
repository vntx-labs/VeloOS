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

USER_INCLUDE = -I./include -I./include/libc -I./include/velo

# ==========================================
# BOOTLOADER COMPILER-FLAGS (UEFI APPLICATION)
# ==========================================
BOOT_CFLAGS = -I$(EFI_INCLUDE) \
              -I$(EFI_INC_ARCH) \
              -I./include \
              -fno-stack-protector \
              -fpic \
              -fshort-wchar \
              -mno-red-zone \
              -march=x86-64 \
              -fno-builtin \
              -Wall \
              -Wextra \
              -O2

# ==========================================
# FREESTANDING KERNEL COMPILER-FLAGS (KEIN EFI!)
# ==========================================
KERN_CFLAGS = -I./include \
              -fno-stack-protector \
              -fno-builtin \
              -fno-asynchronous-unwind-tables \
              -fno-unwind-tables \
              -mno-red-zone \
              -march=x86-64 \
              -mno-sse3 \
              -mno-ssse3 \
              -mno-sse4.1 \
              -mno-sse4.2 \
              -fno-tree-loop-distribute-patterns \
              -Wall \
              -Wextra \
              -O2

# ==========================================
# USERLAND APP COMPILER-FLAGS (RING 3 APPS)
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
              intro.o \
              wallpaper.o

.PHONY: all clean run reset-disk disk-reset apps bin_tools libc sync-disk update-disk

all: os.img sync-disk

# ==========================================
# 1. WALLPAPER CONVERSION & LINKING
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
# 2. CUSTOM UEFI BOOTLOADER (BOOTX64.EFI)
# ==========================================
bootloader.o: bootloader.c
	$(CC) $(BOOT_CFLAGS) -c bootloader.c -o bootloader.o

bootloader.efi: bootloader.o
	$(LD) -nostdlib -znocombreloc -shared -Bsymbolic \
	  -T /usr/lib/elf_x86_64_efi.lds \
	  /usr/lib/crt0-efi-$(ARCH).o \
	  bootloader.o \
	  -L/usr/lib -lefi -lgnuefi \
	  -o bootloader.so
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
	  bootloader.so \
	  bootloader.efi

# ==========================================
# 3. FREESTANDING BARE-METAL KERNEL (KERNEL.ELF)
# ==========================================
%.o: %.c
	$(CC) $(KERN_CFLAGS) -c $< -o $@

KERNEL.ELF: $(KERNEL_OBJS)
	$(LD) -nostdlib -T kernel.lds $(KERNEL_OBJS) -o KERNEL.ELF

# ==========================================
# 4. VELOLIBC BAUEN
# ==========================================
libc: libc/velolibc.o
	@mkdir -p lib
	$(AR) rcs lib/libvelo.a libc/velolibc.o

libc/velolibc.o: libc/velolibc.c
	@mkdir -p libc
	$(CC) $(USER_CFLAGS) -c libc/velolibc.c -o libc/velolibc.o

# ==========================================
# 5. USERLAND APPS (apps/*) BAUEN
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
# 6. UNIX COREUTILS (bin/*.c) BAUEN
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
# 7. DISK-IMAGE INITIALISIERUNG
# ==========================================
os.img: bootloader.efi KERNEL.ELF apps bin_tools
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
	@$(MAKE) sync-disk

# ==========================================
# 8. LIVE-SYNCHRONISATION (OHNE DISK-RESET)
# ==========================================
update-disk: sync-disk
sync-disk: bootloader.efi KERNEL.ELF apps bin_tools
	@echo "[+] Aktualisiere Systemdateien und Apps auf os.img..."
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
	@mcopy -o -i os.img@@1M bootloader.efi ::/EFI/BOOT/BOOTX64.EFI
	@mcopy -o -i os.img@@1M KERNEL.ELF ::/KERNEL.ELF
	@mcopy -o -i os.img@@1M KERNEL.ELF ::/EFI/BOOT/KERNEL.ELF
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
	@if [ -f nvmedisk.img ]; then \
		if mdir -i nvmedisk.img@@1M ::/ 2>/dev/null >/dev/null; then \
			echo "[+] Synchronisiere Updates auf installierte NVMe SSD (nvmedisk.img)..."; \
			mcopy -o -i nvmedisk.img@@1M KERNEL.ELF ::/KERNEL.ELF 2>/dev/null || true; \
			mcopy -o -i nvmedisk.img@@1M KERNEL.ELF ::/EFI/BOOT/KERNEL.ELF 2>/dev/null || true; \
			mcopy -o -i nvmedisk.img@@1M bootloader.efi ::/EFI/BOOT/BOOTX64.EFI 2>/dev/null || true; \
			if [ -d bin_out ]; then \
				for b in bin_out/*.BIN; do \
					if [ -f "$$b" ]; then \
						fname=$$(basename "$$b"); \
						mcopy -o -i nvmedisk.img@@1M "$$b" "::/bin/$$fname" 2>/dev/null || true; \
					fi; \
				done; \
			fi; \
			for app in *.BIN; do \
				if [ -f "$$app" ] && [ "$$app" != "WALL.BIN" ]; then \
					mcopy -o -i nvmedisk.img@@1M "$$app" "::/$$app" 2>/dev/null || true; \
					mcopy -o -i nvmedisk.img@@1M "$$app" "::/bin/$$app" 2>/dev/null || true; \
					mcopy -o -i nvmedisk.img@@1M "$$app" "::/Programs/$$app" 2>/dev/null || true; \
				fi; \
			done; \
		fi; \
	fi

run: os.img sync-disk
	@if [ ! -f nvmedisk.img ]; then \
		echo "[+] Erstelle 2 GB PCIe NVMe SSD (nvmedisk.img)..."; \
		dd if=/dev/zero of=nvmedisk.img bs=1M count=2048 status=none; \
	fi
	qemu-system-x86_64 \
	  -cpu host \
	  -m 4096M \
	  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
	  -machine q35 \
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
	rm -f *.o *.so *.efi *.BIN *.bin KERNEL.ELF bootloader.efi bootloader.so libc/*.o lib/*.a esp.img efiboot.img velo.iso wallpaper.bin wallpaper.o
	rm -rf dist iso_root bin_out /tmp/litehtml_build
	@echo "[+] Build-Dateien aufgeraeumt."
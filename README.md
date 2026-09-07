# V12 Operating System

V12 is an advanced, custom-built 64-bit operating system kernel for x86_64 architectures, written in C and built from scratch. It boots via UEFI, features its own custom graphical compositor, window manager, FAT32 file system driver, NVMe/AHCI disk controllers, TCP/IP network stack, ELF64 userland executable loader, POSIX-compatible command line utilities, and the `litehtml` rendering engine integration.

---

## 🚀 Key Features

- **UEFI Boot & Native Graphics (GOP):** Boots natively via UEFI with a custom Graphics Output Protocol (GOP) linear framebuffer compositor supporting multi-buffering and dynamic resolution switching.
- **Multitasking & Scheduler:** Preemptive round-robin scheduler supporting kernel and user tasks with isolated address spaces and automated page-fault handling.
- **Virtual Memory & Paging (PMM/VMM):** Robust Physical Memory Manager (PMM) and Virtual Memory Manager (VMM) with x86_64 4-level paging and demand paging.
- **Window Manager (WM):** Fully-featured graphical window manager supporting window decoration, moving, resizing, minimize/maximize, modal dialogs, and native widget rendering (buttons, icons, storage bars, searchboxes).
- **Storage Subsystem:** Native drivers for AHCI (SATA) and NVMe controllers coupled with an integrated FAT32 file system parser and virtual path resolution (`C:`, `/bin`, `/Programs`).
- **Networking Stack:** Integrated network driver stack supporting DHCP, DNS resolution, TCP sockets, TLS bridging, and synchronous/asynchronous HTTP client requests.
- **ELF64 Userland & Executable Loader:** Supports running dynamic ELF64 user binaries with `R_X86_64_RELATIVE` relocation support, protected user ring-3 transitions, and system call interface (`int 0x80`).
- **TTY & Rescue Shell:** Multi-TTY management (`Ctrl+Alt+1` through `8`) allowing seamless switching between the graphical desktop environment and kernel/emergency shells (`kshell`).

---

## 📁 Directory Structure

```text
V12/
├── apps/                 # Graphical applications (Browser, Explorer, Notepad, Shell)
├── bin/                  # Source files for core CLI command-line utilities (cat, cp, ls, grep, etc.)
├── bin_out/              # Compiled binary outputs (.BIN)
├── include/              # Public headers (libc headers & Velo OS system interfaces)
├── libc/                 # Velibc runtime support library
├── libs/                 # External libraries (litehtml HTML rendering engine, cxx_runtime)
├── ahci.c / .h           # AHCI SATA disk controller driver
├── nvme.c / .h           # NVMe storage controller driver
├── fat32.c / .h          # FAT32 file system driver
├── kernel.c              # Main kernel entry, GOP initialization, and GUI/Network workers
├── sched.c / .h          # Preemptive task scheduler, IDT/GDT setup, and exception handlers
├── syscall.c / .h        # System call dispatcher and ELF64 loader
├── wm.c / .h             # Window manager and graphical compositor
├── net.c / .h            # Network stack, sockets, DNS, and HTTP implementation
├── keyboard.c / .h       # Keyboard driver with QWERTZ/QWERTY layout translation
├── mouse.c / .h          # Mouse driver and cursor management
├── desktop.c / .h        # Graphical desktop environment & launcher
└── Makefile              # Build configuration for compilation and ISO generation
```

---

## 🛠️ Build & Compilation

To build the V12 operating system image and compile all associated binaries and user applications, ensure you have an x86_64 cross-compiler toolchain (`x86_64-elf-gcc` or equivalent) and standard build utilities installed.

```bash
# Clone or navigate to the V12 root directory
cd V12

# Build kernel, binaries, and generate the bootable ISO image
make
```

### Generated Targets
- `kernel.efi`: UEFI kernel executable.
- `*.BIN`: Compiled user space binaries.
- `velo.iso`: Complete bootable OS ISO image.

---

## ⌨️ Shortcuts & Controls

- **`Ctrl + Alt + 1`**: Switch to Desktop Environment (TTY 1).
- **`Ctrl + Alt + 2` – `8`**: Switch to Emergency Kernel Shells / TTYs.
- **Window Controls**: Title bar drag to move, window border handles to resize, and native button handlers for close/maximize.

---

## 📄 License

Distributed under the terms of the included LICENSE file. Refer to individual library directories (such as `litehtml`) for their respective licensing terms.
![visitors](https://laobi.icu/badge?page_id=vntx-labs.visitor-badge&left_text=Total%20Visitors%3A&left_color=%231a5fb4&right_color=%231a5fb4&radius=10&height=25)
# VeloOS (V12.2.0)

> **Modern, Autonomous UEFI x86_64 Operating System & Desktop Environment**

VeloOS is a custom-built, modern 64-bit operating system kernel written for x86_64 UEFI firmware. It features a complete native graphical window manager (WM), preemptive multitasking scheduler, virtual memory management (VMM/PMM), a robust POSIX-like system call interface, native ELF64 binary execution, network stack (IPv4/TCP/UDP/DNS/HTTP/TLS), and an integrated suite of desktop applications.

---

## 🚀 Key Features & Architecture

### 1. Kernel & Bootloader
- **Pure UEFI Boot:** Boots natively via `kernel.efi` with GOP (Graphics Output Protocol) acceleration.
- **Robust Memory Management:** Physical Memory Manager (PMM) parses UEFI memory maps; Virtual Memory Manager (VMM) handles page-fault driven on-demand mapping for heap and stacks.
- **Preemptive Multitasking Scheduler:** 100 Hz APIC Timer-driven round-robin scheduler supporting multiple kernel tasks and isolated user-space processes.
- **Ring 3 Protection & Syscalls:** Full support for User/Kernel privilege rings, x86_64 GDT/TSS configuration, exception handling, and `int 0x80` / custom syscall bridges.

### 2. Window Manager & Compositor (WM)
- **Autonomous GUI Compositor:** Smooth hardware-accelerated backbuffer swapping, window dragging, resizing, maximizing, and modal dialogs.
- **TTY Switching (TTY 1–8):** Instant context switching via `Alt + 1` through `Alt + 8`. TTY 1 runs the full desktop environment, while TTY 2–8 launch persistent, isolated emergency KShell sessions.
- **Magic Recovery Sequence:** Press `Alt + Delete` and type `desktop` to instantly terminate hung states, kill rogue processes, and reload the desktop environment.

### 3. File System & Storage
- **AHCI & NVMe Drivers:** Low-level disk controller support for SATA (AHCI) and high-speed NVMe solid-state drives.
- **FAT32 File System Driver:** Native robust read, write, directory listing, creation, and manipulation of files across partitions (C:, etc.).

### 4. Networking Stack
- Network polling loop supporting Ethernet, IPv4, UDP/TCP sockets, DNS resolution, HTTP requests, and secure TLS socket communication.

---

## 📂 Project Directory Structure

```text
V12.2.0/
├── apps/               # Native GUI applications (Browser, Explorer, Notepad, Sh)
├── bin/                # Core command-line utility source files (cat, ls, grep, etc.)
├── bin_out/            # Compiled binary executables (.BIN)
├── include/            # System headers (libc, velo syscalls, network, window manager)
├── libc/               # Velo C Library runtime (`velolibc.c`)
├── libs/               # Third-party libraries (litehtml rendering engine)
├── ahci.c / .h         # SATA AHCI disk controller driver
├── nvme.c / .h         # NVMe storage driver
├── fat32.c / .h        # FAT32 file system implementation
├── kernel.c            # Kernel entry point, GOP setup, TTY & compositor supervisor
├── sched.c / .h        # APIC timer, GDT/TSS, IDT, and preemptive task scheduler
├── syscall.c / .h      # System call router & ELF64 loader with relocations
├── wm.c / .h           # Graphical window manager and UI widget renderer
├── desktop.c / .h      # Desktop GUI environment (taskbar, launcher, desktop icons)
├── kshell.c / .h       # Kernel rescue shell for TTY 2–8
├── net.c / .h          # Network stack and socket management
└── Makefile            # Build configuration and ISO packaging script
```

---

## 🛠️ Included Applications & Utilities

### GUI Applications (`/apps/`)
- **Browser (`browser.c`):** Integrated web browser powered by the embedded `litehtml` rendering engine.
- **Explorer (`explorer.c`):** Graphical file manager for navigating directories, copying, and managing files.
- **Notepad (`notepad.c`):** Text editor for creating and modifying documents.
- **Terminal Shell (`sh/`):** Graphical shell environment executing system commands.

### Command-Line Utilities (`/bin/`)
VeloOS includes standard Unix-like utilities compiled as native binaries:
`cat`, `cp`, `date`, `df`, `echo`, `free`, `grep`, `head`, `help`, `hostname`, `kill`, `ls`, `mkdir`, `mv`, `ps`, `reboot`, `rm`, `shutdown`, `tail`, `touch`, `uname`, `wc`, `whoami`.

---

## ⚙️ Building and Running

### Prerequisites
- GCC / Binutils cross-compiler toolchain targeting `x86_64-elf`
- `make`, `mtools`, and `genisoimage` (or `xorriso`) for building the UEFI bootable ISO image (`velo.iso`).

### Compilation
To build the complete operating system image and compile all user binaries:
```bash
make
```

### Execution
Run the generated UEFI image inside an emulator like QEMU:
```bash
qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd -drive file=velo.iso,format=raw -m 512M -net nic,model=e1000 -net user
```

---

## 📜 License
This project is open-source. See the [LICENSE](LICENSE) file for details.
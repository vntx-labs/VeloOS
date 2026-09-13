![visitors](https://laobi.icu/badge?page_id=vntx-labs.visitor-badge&left_text=Total%20Visitors%3A&left_color=%231a5fb4&right_color=%231a5fb4&radius=10&height=25)
# VeloOS - Patch Notes & Release Readme (v12.3.0)

Welcome to **VeloOS v12.3.0**, a major incremental release focusing on architectural hardening, robust multitasking isolation, and expanded low-level stability. This document outlines the key updates, bug fixes, and technical enhancements introduced in version 12.3.0.

---

## 🚀 Overview & Key Highlights

Version 12.3.0 introduces critical improvements to the x86_64 kernel core, memory management unit (MMU/PMM/VMM), hardware-level process isolation, and the window compositor subsystem. It also includes comprehensive fixes for input polling, exception handling, and device driver communication.

---

## 🛠️ Detailed Patch Notes & Changes

### 1. Kernel Architecture & Memory Management (`kernel.c`, `pmm_vmm.h`, `sched.c`)
- **Robust GOP Initialization (`init_gop`):** Improved Graphic Output Protocol fallback and resolution management. The system now securely requests 1024x768 or safely retains firmware-standard resolutions with resilient multi-buffer allocations.
- **Hardware-Checked SSE Initialization (`enable_sse`):** Implemented strict CPUID checks (EDX bits 24 and 25 for FXSR and SSE) before setting CR0/CR4 bits (`OSFXSR`, `OSXMMEXCPT`), preventing illegal instruction exceptions on legacy or specialized virtualized hardware.
- **Real RAM Detection (`detect_real_ram_from_mmap`):** Upgraded UEFI memory map parsing to accurately accumulate conventional, loader, and boot services memory sectors into a unified RAM counter (`g_total_ram_mb`).
- **Autonomic TTY & Session Controller:** Enhanced TTY switching (`Alt + 1` through `Alt + 8`) and implemented a robust magic recovery/supervisor sequence (`Alt + Del` followed by typing `desktop`) to cleanly restart or switch out of frozen UI sessions without rebooting the kernel.

### 2. Preemptive Multitasking & PML4 Isolation (`sched.c`)
- **True PML4 Context Switching:** Each user task now operates within its own hardware-enforced address space (`cr3_pml4`). During APIC timer preemption ticks (`sched_schedule_c`), the scheduler seamlessly switches the `CR3` register and saves/restores floating-point/SSE state via `fxsave64`/`fxrstor64`.
- **Demand Paging & Page Fault Exception Handling:** Integrated `#PF` (Page Fault, Exception 14) handling directly into the kernel exception stub. Userland heaps and stack growth trigger transparent on-demand page mapping (`vmm_handle_page_fault`).
- **APIC Timer & IDT Alignment:** Configured Local APIC timer division and vector routing with dedicated interrupt service routines (ISRs) and interrupt stack tables (IST) for critical exceptions (Division Error, Invalid Opcode, Double Fault, General Protection Fault, and Page Fault).

### 3. Syscall Subsystem & ELF Application Loader (`syscall.c`)
- **Isolated ELF64 Loader:** Enhanced `load_and_run_app` to parse ELF64 headers (`Elf64_Ehdr`, `Elf64_Phdr`, `Elf64_Shdr`) and perform dynamic relocation (`Elf64_Rela`) for text/data segments inside isolated virtual address spaces.
- **Extended System Call API (`SYS_EXIT`, `SYS_EXEC_APP`, `SYS_KILL_TASK`, etc.):** 
  - Added robust window handle tracking (`g_app_win_ids`) ensuring that when an application exits or is terminated via `SYS_KILL_TASK`, its GUI windows are automatically cleaned up.
  - Implemented secure cross-port file copy, directory creation, file moving, and system power management (`SYS_SYSTEM_REBOOT`, `SYS_SYSTEM_SHUTDOWN`).
  - Added network socket and HTTP asynchronous query support (`SYS_SOCKET_OPEN`, `SYS_HTTP_GET`, `SYS_DNS_RESOLVE`).

---

## 📦 File Structure Snapshot (v12.3.0)

```text
V12.3.0/
├── apps/              # Graphical user applications (browser, explorer, notepad, sh, viper)
├── bin/               # Core command-line utilities (cat, cp, ls, grep, ps, reboot, etc.)
├── include/           # C standard library and VeloOS system headers
├── libs/              # Embedded libraries (litehtml, cxx_runtime, etc.)
├── kernel.c           # Core kernel initialization, GOP, TTY supervisor, event loop
├── sched.c            # Multitasking scheduler, GDT/TSS, IDT, APIC timer, exception stubs
├── syscall.c          # Ring 3 system call handler and secure ELF64 loader
├── pmm_vmm.h          # Physical and virtual memory management declarations
├── setup.h            # System configuration structures and setup state machine
└── os.img / nvmedisk  # Bootable disk images and storage assets
```

---

## ⚙️ Compilation & Installation

To build and run VeloOS v12.3.0 from source:

1. Ensure a modern cross-compiler toolchain (`x86_64-elf-gcc`, `ld`, `make`, `mtools`) is installed.
2. Run the main build target from the root directory:
   ```bash
   make clean
   make all
   ```
3. Test the image using QEMU with UEFI firmware (EDK2):
   ```bash
   qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.code.fd -drive file=os.img,format=raw -m 512M -net nic,model=e1000 -net user
   ```

---
*VeloOS Development Team — Release Documentation v12.3.0*
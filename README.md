![visitors](https://laobi.icu/badge?page_id=vntx-labs.visitor-badge&left_text=Total%20Visitors%3A&left_color=%231a5fb4&right_color=%231a5fb4&radius=10&height=25)
# VeloOS - Version 11 (Multitasking, Paging & HTML Engine Bridge)

VeloOS V11 hebt das Betriebssystem auf eine neue Stufe der nativen Systemarchitektur: Echtes präemptives Multitasking mit APIC-Timer-Interrupts, vollständiges x86_64 Paging-Management mit On-Demand Page Fault Handling sowie die Integration der C++ Laufzeitumgebung zur hardwarebeschleunigten Darstellung von HTML-Inhalten über das LiteHTML-Framework.

## Neuheiten & Meilensteine
- Präemptiver Task-Scheduler & Interrupts (`sched.c/.h`):
  - Hardware-Multitasking via x86_64 GDT, IDT und Task State Segment (TSS).
  - Unabhängige Task-Infrastruktur für Kernel- und User-Mode (Ring 3 Privilege Separation) mit separaten Stacks.
  - LAPIC-Timer-Interrupt-gesteuerter 100 Hz Scheduler mit automatischem State-Saving (`fxsave64`/`fxrstor64` für FPU/SSE-Register).
- Virtuelles Speichermanagement & Paging (`pmm_vmm.h`, `pmm_vmm.c`):
  - Dynamische UEFI Memory Map Auswertung zur Initialisierung des Physical Page Allocators (PMM).
  - On-Demand Page Fault Handler zur dynamischen Allokation und Erweiterung von User-Heaps und Stack-Pages (CR2-Überwachung).
  - Flexibles virtuelles Adressraum-Mapping (PML4, PDPT, PD, PT) mit Benutzer- und Schreibschutz-Flags.
- Asynchrone Netzwerk- & HTTP-Engine (`net.c/.h`):
  - Hardware-Treiber für Intel E1000 Gigabit-Ethernet (PCI-MMIO-BAR-Erkennung, Ringspeicher-Handling).
  - Nativer Netzwerk-Stack: ARP (Request/Reply), UDP, DNS-Auflösung mit 16-Einträge-Cache sowie TCP-Zustandsautomat.
  - Asynchroner HTTP-Engine mit Non-Blocking Polling (`net_http_async_start`/`poll`) inklusive Instant-Cancel und Safe-Reset (RST/ACK-Signalisierung).
- LiteHTML C++ Browser-Engine Bridge (`libs/litehtml/`, `litehtml_bridge.cpp`):
  - Bereitstellung einer C++ Laufzeitumgebung (`cxx_runtime.cpp`) im Kernel-Umfeld (Überladung von `new`/`delete`).
  - Native Anbindung des LiteHTML-CSS-Renderers über eine C-Userland-Bridge (`velo_litehtml_draw`).
  - Event-basiertes Rendering von Texten, Tabellen (`el_tr.cpp`, `el_td.cpp`) und Listenmarkern direkt auf Fenster-Surfaces via Fenster-Manager-Callbacks.
- Erweitertes FAT32-Dateisystem & NVMe-Support (`fat32.c`, `nvme.c`):
  - Nativer NVMe-PCIe-Treiber mit Admin- und I/O-Submission/Completion-Queues über Doorbell-Register.
  - Vollständiges FAT32-Management: Unterstützung für Datei-Erstellung, Löschung (`fat32_delete_file`), Verzeichnisnavigation (`mkdir`) und CMOS-Echtzeitstempel.

## Verzeichnisstruktur
V11/ \
├── apps/               # Systemapplikationen (browser, explorer, notepad, sh) \
├── include/            # C-Standardbibliothek-Header (libc) & VeloOS-Userland-API \
├── libc/               # Zentrale velolibc.c Implementierung \
├── libs/                \
│   ├── litehtml/       # Kompletter LiteHTML HTML5/CSS-Parser Quellcode (C++17) \
│   ├── cxx_runtime.cpp # C++ Kernelland-Laufzeitumgebung (new/delete Overloads) \
│   └── litehtml_bridge.cpp # C-Schnittstelle zwischen OS-Grafik und HTML-DOM-Renderer \
├── kernel.c            # Kernel-Hautproutine, Grafik-Initialisierung (GOP) & Worker-Tasks \
├── sched.c             # GDT, IDT, TSS, LAPIC-Timer-ISR & Task-Wechsel-Infrastruktur \
├── pmm_vmm.c           # Physikalischer Page-Allocator & Virtuelle PML4-Tabellen-Verwaltung \
├── syscall.c           # System-Call-Handler (SYS_EXIT, SYS_CREATE_WINDOW, SYS_HTTP_ASYNC_START etc.) \
├── net.c               # Intel E1000 Netzwerktreiber, TCP/UDP-Stack, DHCP & Async-HTTP-Engine \
├── fat32.c / ahci.c    # FAT32-Dateisystem, Verzeichnis-Operationen & SATA-AHCI-Treiber \
├── nvme.c              # NVMe PCIe SSD Controller-Treiber (Submission/Completion-Queues) \
├── keyboard.c          # Scancode-Decoder & deutsches QWERTZ-Tastaturlayout \
└── Makefile            # Kernel- & Applikations-Buildsystem

## Build & Ausführung
Befehl: make clean && make run

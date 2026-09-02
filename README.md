![visitors](https://laobi.icu/badge?page_id=vntx-labs.visitor-badge&left_text=Total%20Visitors%3A&left_color=%231a5fb4&right_color=%231a5fb4&radius=10&height=25)
# VeloOS - Version 10.0.0 (Virtual Memory Isolation, Demand Paging & 4D Wheel Mouse)

VeloOS V10.0.0 vollzieht den entscheidenden architektonischen Schritt zu einem modernen, hardware-isolierten Betriebssystem: Einführung von virtuellem Speichermanagement (VMM), On-Demand Paging, echter Ring-3-Isolation und voller 4D-Mausrad-Unterstützung.

## Neuheiten & Meilensteine
- Physical & Virtual Memory Management (pmm_vmm.c / pmm_vmm.h):
  - Bitmap-basierter Physical Memory Manager (PMM) mit nativer Unterstützung für bis zu 16 GB physischen RAM.
  - Virtual Memory Manager (VMM): Erstellt isolierte 4-Level-PML4-Seitentabellen für Userland-Tasks mit gespiegeltem Kernel-Space in den oberen 2 GB.
  - On-Demand Paging: Fängt Page Faults (Exception 14 / CR2) innerhalb definierter Heap-Grenzen transparent ab und weist dynamisch physische Frames zu.
- Echte Ring-3 Prozess-Isolation (sched.c / syscall.c):
  - Jeder Task erhält seinen eigenen CR3-Adressraum (`cr3_pml4`), eigenen User-Stack und dedizierten Ausführungsspeicher (`g_app_exec_memory[MAX_TASKS]`).
  - Schutz der unteren 16 MB Systemspeicher vor DMA- und Kernel-Überschreibungen.
- PS/2 IntelliMouse 4D-Scrollrad-Treiber (mouse.c / mouse.h):
  - Initialisierung der IntelliMouse Magic Sequence für 4-Byte-Streaming.
  - Erkennung und Verarbeitung von vertikalem Mausrad (`scroll_z`) und horizontalem Tilt-Wheel (`scroll_h`).
- Echtes 2D-Flüssigscrolling in Notepad:
  - Windows-konforme Scrollrad-Richtung (Drehung nach unten scrollt Text nach oben).
  - Horizontale und vertikale Scrollbars mit präziser Begrenzung (Bounds-Clipping) und flüssigem Maus-Dragging.
- Exaktes Case-Preserving FAT32:
  - Vollständige Beibehaltung der Groß-/Kleinschreibung beim Dateinamen-Matching ohne Datenverlust im 8.3-Standard.

## Verzeichnisstruktur
veloos/ \
├── pmm_vmm.c/.h     # PMM (Bitmap) & VMM (PML4 Isolation, Page Faults) \
├── sched.c/.h       # Multi-CR3 Scheduler & FPU/SSE fxsave Context-Switch \
├── syscall.c        # Isolierter ELF-Loader & Task-Dispatcher \
├── mouse.c/.h       # IntelliMouse 4-Byte Treiber (Vertikal- & Tilt-Wheel) \
├── apps/notepad/ \
│   └── notepad.c    # 2D-Smooth-Scrolling mit Mausrad & Dragging \
└── Makefile

## Build & Ausführung
Befehl: make clean && make run
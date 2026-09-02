![visitors](https://laobi.icu/badge?page_id=vntx-labs.visitor-badge&left_text=Total%20Visitors%3A&left_color=%231a5fb4&right_color=%231a5fb4&radius=10&height=25)
# VeloOS - Version 5.0.0 (VeloLIBC & Echte Ring-3 Userland-Architektur)

VeloOS V5.0.0 markiert die Geburtsstunde eines vollwertigen Anwendungs-Ökosystems: Eine eigene C-Standard-Bibliothek (VeloLIBC), Hardware-Isolation und die Auslagerung von Systemprogrammen (wie dem Datei-Explorer) in den Ring-3-Userspace.

## Neuheiten & Meilensteine
- Eigene C-Standardbibliothek (VeloLIBC / libvelo.a):
  - Bereitstellung von POSIX-kompatiblen Headern: <stdio.h>, <stdlib.h>, <string.h>, <ctype.h>.
  - Dynamischer Heap-Allokator: Eigener First-Fit/Chunk-Algorithmus mit malloc(), free(), calloc(), realloc().
  - Formatierte String-Ausgabe: sprintf(), snprintf(), vsnprintf().
- Kernel-Syscall-Gate über Interrupt 0x80:
  - Universeller Syscall-Vektor bei 0x80000 im Speicher.
  - Systemaufrufe für Fensterverwaltung, Grafik-Primitives, Heap-Allokation, Dateisystemzugriff und Prozess-Lifecycle.
- Fenster-Surface-Architektur:
  - Jedes Fenster besitzt nun eine eigene Offscreen-Grafikfläche (surface mit 1024x768 Strides).
  - Userland-Anwendungen rendern atomar auf ihre eigene Surface; der Kernel-Compositor blittet die Fensterinhalte ruckelfrei in den Backbuffer.
- Erste vollwertige Ring-3-Applikation: EXPLORER.BIN (apps/explorer.c):
  - Eigenständiges Executable, verknüpft mit libvelo.a.
  - Dateiliste mit Navigation, Breadcrumbs, Ordner-Favoriten, Dateigrößenformatierung und Ausführung von Programmen.
- Hierarchische Dateisystem-Ordnerstruktur:
  - FAT32-Treiber erweitert um Verzeichniserstellung (fat32_mkdir) und Dateiauflistung (fat32_list_dir).
  - Standardverzeichnisse auf der Festplatte: /Windows, /Windows/System32, /Program Files, /Users.

## Verzeichnisstruktur
veloos/ \
├── include/         # Standard-Header \
|   ├── velo/        # Syscall- & Window-Schnittstellen \
│   ├── stdio.h, stdlib.h, string.h, ctype.h \
├── libc/            # VeloLIBC Quellcode \
│   └── velolibc.c   # CRT-Startup (_start) & POSIX-Funktionen \
├── apps/ \
│   └── explorer.c   # Ring-3 Explorer-Anwendung \
├── kernel.c         # Kernel Core & Syscall-Vektor \
├── syscall.c/.h     # Syscall Handler Dispatcher \
├── wm.c/.h          # Window Surfaces & Compositor \
└── Makefile         # Kompiliert Kernel und Ring-3-Apps getrennt

## Build & Ausführung
Befehl: make clean && make run
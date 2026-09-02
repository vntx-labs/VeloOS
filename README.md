![visitors](https://laobi.icu/badge?page_id=vntx-labs.visitor-badge&left_text=Total%20Visitors%3A&left_color=%231a5fb4&right_color=%231a5fb4&radius=10&height=25)
# VeloOS - Version 7.0.0 (Preemptive Scheduler, Desktop VFS & Networking)

VeloOS V7.0.0 verwandelt das Ein-Task-System in ein vollwertiges präemptives Multitasking-Betriebssystem. Gleichzeitig wird der Desktop direkt mit dem FAT32-Dateisystem synchronisiert und um umfassende Dateiverwaltung ergänzt.

## Neuheiten & Meilensteine
- Hardwarenaher Preemptiver Scheduler (sched.c):
  - APIC-Timer angetriebener Round-Robin Scheduler bei 100 Hz Ticks (Interrupt Vektor 0x40).
  - 64-Bit Hardware-TSS (Task State Segment) mit separatem IST1-Notfall-Stack zur Verhinderung von Triple Faults bei CPU-Exceptions (0, 6, 8, 13, 14).
  - Vollständiges Sichern und Wiederherstellen von Universalregistern und SSE/FPU-Zuständen (fxsave64 / fxrstor64).
  - Unterstützung für Threads (task_create), Task-Sleep (task_sleep) und Userspace-Prozesse (task_create_user).
- Dateisystem-Desktop-Synchronisation (/Users/Desktop):
  - Der Desktop liest Icons dynamisch aus dem FAT32-Verzeichnis /Users/Desktop.
  - Rechtsklick-Kontextmenü auf dem Desktop: Neuer Ordner, Neues Dokument, Aktualisieren, Explorer.
  - Doppelklick auf Desktop-Icons startet ausführbare Programme (.BIN/.EFI) oder öffnet Textdateien im Notepad.
- Fortgeschrittenes Datei-Management:
  - Kopieren, Ausschneiden und Einfügen (fat32_copy_file, fat32_move_file).
  - Umbenennen (fat32_rename_file) und Löschen (fat32_delete_file).
  - Intelligenter Dateikonflikt-Dialog: Ersetzen, Überspringen oder beide Dateien behalten (Auto-Renaming).
- Vektorisierte Vantix-Icon-Pipeline (include/velo/icons.h):
  - Reine Integer-Arithmetik (keine Floating-Point Traps) zum Zeichnen von Ordner-, PC-, Dokumenten- und Anwendungs-Icons.
- Erweiterter Netzwerk- & TLS-Stack:
  - Socket-API (SYS_SOCKET_OPEN, SYS_SOCKET_SEND, SYS_SOCKET_RECV), DNS-Auflösung via UDP Port 53, SHA-256 Krypto-Primitiven.

## Verzeichnisstruktur
veloos/ \
├── sched.c/.h       # APIC-Timer Scheduler & Task-Verwaltung \
├── fat32.c/.h       # Dateioperationen (Copy, Move, Rename, Delete) \
├── desktop.c/.h     # Synchronisierter Icon-Desktop & Kontextmenü \
├── apps/explorer/ \
│   └── explorer.c   # Vollwertiger Datei-Manager mit Konflikt-Dialogen \
├── include/velo/ \
│   ├── icons.h      # Vantix Vektor-Icon-Engine \
│   └── net.h        # Socket- & Netzwerk-Header \
└── Makefile \

## Build & Ausführung
Befehl: make clean && make run
![visitors](https://laobi.icu/badge?page_id=vntx-labs.visitor-badge&left_text=Total%20Visitors%3A&left_color=%231a5fb4&right_color=%231a5fb4&radius=10&height=25)
# VeloOS - Version 9.0.0 (Multi-Drive Mounts, Desktop-Minimizing & Kernel Widgets)

VeloOS V9.0.0 optimiert die Benutzeroberfläche und Speicherarchitektur bis ins letzte Detail: Gleichzeitige Verwaltung mehrerer Festplatten (SATA & NVMe), dynamische Größenberechnung ohne Hardcoding und hardware-geschützte UI-Widgets.

## Neuheiten & Meilensteine
- Multi-Volume FAT32 Architektur (fat32.c):
  - Unterstützung für bis zu 8 simultan gemountete Partitionen und Laufwerke (C:, D:, etc.).
  - Vollständig dynamische Erkennung freier Speicherkapazitäten (fat32_get_free_bytes) über authentische FSInfo-Sektoren und FAT-Sampling (keine Dummy-Werte mehr).
- Desktop-Minimierung ("Show Desktop"):
  - Ein Klick auf eine freie Stelle des Desktops minimiert blitzschnell alle geöffneten Anwendungsfenster (wm_minimize_all) und setzt den Fokus zurück auf die Arbeitsfläche.
- Erweitertes Fensterlimit:
  - Erhöhung des Systemlimits auf bis zu 64 gleichzeitig verwaltbare Fenster (MAX_WINDOWS 64).
- Kernel-Protected UI Widget Syscalls:
  - Auslagerung rechenintensiver UI-Zeichenoperationen in den Kernel:
    - SYS_DRAW_BUTTON, SYS_DRAW_STORAGE_BAR, SYS_DRAW_SIDEBAR_ITM
    - SYS_DRAW_NAV_BTN, SYS_DRAW_ADDR_BAR, SYS_DRAW_SEARCHBOX, SYS_DRAW_DIALOG
  - Reduziert Code-Größe und Komplexität in Userland-Programmen drastisch und garantiert konsistente Render-Qualität.
- Userland Page-Table Protection:
  - enable_user_paging: Schaltet die Userland-Pages (g_exec_area, g_user_heap, User-Stacks) in den Page Tables auf User-Accessible (U/S = 1), um Ring-3-Ausführungen hardwareseitig zu schützen.

## Verzeichnisstruktur
veloos/ \
├── syscall.c/.h     # Erweiterter Syscall-Satz (Widget-Delegation) \
├── wm.c/.h          # 64-Window Compositor & Widget-Renderer \
├── fat32.c/.h       # Multi-Volume FAT32 Engine \
├── desktop.c        # Show-Desktop Klick-Handling & Icons \
├── apps/explorer/ \
│   └── explorer.c   # Multi-Drive Explorer mit Laufwerkskacheln \
└── Makefile

## Build & Ausführung
Befehl: make clean && make run
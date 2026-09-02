![visitors](https://laobi.icu/badge?page_id=vntx-labs.visitor-badge&left_text=Total%20Visitors%3A&left_color=%231a5fb4&right_color=%231a5fb4&radius=10&height=25)
# VeloOS - Version 6.0.0 (Aero Glass Explorer & Responsive UI Engine)

In Version 6.0.0 wird der Velo Explorer zu einer responsiven, tief integrierten Datei- und Systemverwaltung ausgebaut. Fenster können dynamisch maximiert werden, während das Rendering flexibel auf Größenänderungen reagiert.

## Neuheiten & Meilensteine
- Responsives Fenstermanagement (Maximize / Restore):
  - Vollständige Unterstützung für Fenster-Maximierung (is_maximized).
  - Automatische Übermittlung von Resize-Events (VELO_EV_RESIZE) an Userland-Applikationen.
  - Dynamische Anpassung aller Spaltenbreiten, Seitenleisten und Details-Panels.
- Windows Vista Explorer Refactoring (apps/explorer.c):
  - Aero Glass Command Bar: Werkzeugleiste mit Aktionen (Organisieren, Öffnen, Neuer Ordner).
  - Interaktive Breadcrumbs: Klickbare Pfadsegmente und Navigations-Historie (Zurück / Vor).
  - Echtzeit-Dateifilter: Suchleiste mit In-Memory-Substring-Suche nach Dateinamen.
  - Dateityp-Erkennung: Zuordnung von Icons und Beschreibungen (Dateiordner, Anwendung, Dokument).
  - Details Pane: Statuszeile am unteren Fensterrand mit Dateiinformationen und primärem Aktions-Button.
- Erweiterte System-Syscalls:
  - SYS_GET_SYSINFO: Ermittelt Host-CPU-Modell (CPUID Leaf 0x80000002–0x80000004) und verfügbaren Arbeitsspeicher (UEFI Memory Map).
  - SYS_GET_DRIVE_INFO: Liefert Festplattenmodell, LBA-Kapazität und freien Speicherplatz.
- Optimierte Text-Layout-Engine:
  - Bricht überlange Dateinamen und Systemtexte intelligent am letzten Leerzeichen um (wm_draw_text_wrapped).

## Verzeichnisstruktur
veloos/ \
├── apps/ \
│   └── explorer.c   # Responsiver Velo Explorer \
├── include/velo/ \
│   ├── window.h     # High-Level Aero Widget Toolkit \
│   └── syscall.h    # System- & Drive-Info Syscalls \
├── libc/velolibc.c  # Erweiterte C-Bibliothek \
├── desktop.c        # Startmenü mit Explorer-Verknüpfung \
├── wm.c             # Multi-Surface Window Engine \
└── Makefile \

## Build & Ausführung
Befehl: make clean && make run
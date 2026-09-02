![visitors](https://laobi.icu/badge?page_id=vntx-labs.visitor-badge&left_text=Total%20Visitors%3A&left_color=%231a5fb4&right_color=%231a5fb4&radius=10&height=25)
# VeloOS - Version 9.1.0 (Advanced File Dialogs, Desktop Sync & Complete UI Overhaul)

VeloOS V9.1.0 bringt die Benutzeroberfläche und Dateiinteraktion auf Desktop-Niveau: Ein universeller grafischer Dateidialog (GFD), automatischer Desktop-Dateisystem-Sync, erweiterte Desktop-Kapazitäten sowie ein komplett überarbeitetes Notepad mit Windows-typischen Menüstrukturen.

## Neuheiten & Meilensteine
- Advanced Graphical File Dialog (GFD in include/velo/window.h):
  - Universeller, wiederverwendbarer Open- & Save-As-Dialog für alle Ring-3-Anwendungen (`VeloFileDialog`).
  - Native Ordnernavigation (`..`, Unterverzeichnisse), Schnellanlegen neuer Ordner direkt im Dialog (`+ Neuer Ordner`) sowie Dateityp-Filterung (`*.txt` / `*.*`).
  - Case-Preserving-Dateinamenverwaltung für 100% konsistente Dateisystem-Einträge ohne Datenverlust.
- Überarbeitetes Notepad (apps/notepad/notepad.c):
  - Integration des neuen Graphical File Dialogs zum nahtlosen Öffnen und Speichern von Dokumenten.
  - Windows-typische Menüleiste (Datei, Bearbeiten, Format) mit Tastatur-Shortcuts (`Ctrl+N`, `Ctrl+O`, `Ctrl+S`).
  - Dynamischer Zeilenumbruch (Word-Wrap), Text-Cursor-Positionierung per Mausklick und Statusleiste mit Zeilen-, Spalten- und Zeichenzähler.
- Erweiterte Desktop-Architektur (desktop.c):
  - Verdoppelung der Desktop-Icons auf bis zu 256 Elemente (`MAX_DESKTOP_ICONS 256`).
  - Automatischer Datei-Sync-Timer (alle 30 Ticks), der Änderungen auf `C:/Users/Desktop/` im Hintergrund erkennt und die Icons live aktualisiert.
  - Intelligente Cursor-Umschaltung: Kontextabhängige Darstellung von Pfeil-, Hand- und I-Beam-Textcursor je nach Fensterbereich und modalem Dialogstatus.
- Explorer Datei-Konflikt-Management (apps/explorer/explorer.c):
  - Robuster Kollisions-Handler bei Dateioperationen: Fragt bei Duplikaten nach Ersetzen, Überspringen oder automatischem Umbenennen ("Beide behalten").
  - Globale Zwischenablage für Kopieren, Ausschneiden und Einfügen (`Ctrl+C`, `Ctrl+X`, `Ctrl+V`).

## Verzeichnisstruktur
veloos/ \
├── include/velo/ \
│   └── window.h     # Graphical File Dialog (GFD) Engine \
├── apps/ \
│   ├── explorer/ \
│   │   └── explorer.c # Dateikonflikt-Dialog & Zwischenablage \
│   └── notepad/ \
│       └── notepad.c  # Neues modales Notepad mit Menüleiste \
├── desktop.c        # 256 Icons, Sync-Timer & Cursor-Routing \
└── Makefile

## Build & Ausführung
Befehl: make clean && make run
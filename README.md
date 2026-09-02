![visitors](https://laobi.icu/badge?page_id=vntx-labs.visitor-badge&left_text=Total%20Visitors%3A&left_color=%231a5fb4&right_color=%231a5fb4&radius=10&height=25)
# VeloOS - Version 4.0.0 (Windows Vista Aero Experience & Out-Of-Box Setup)

VeloOS V4.0.0 konzentriert sich auf ein immersives Benutzererlebnis. Es transformiert die grafische Oberfläche in das ikonische Windows Vista Aero-Design und führt einen vollwertigen Installations- und Einrichtungs-Assistenten (OOBE) ein.

## Neuheiten & Meilensteine
- Interaktiver Setup-Assistent (setup.c):
  - Schritt-für-Schritt Installationsdialog mit Vista-Aero-Header und Windows-Flagge:
    1. Sprach- & Tastaturauswahl: Deutsch (QWERTZ) oder Englisch (QWERTY).
    2. Zeit & Region: Zeitzonenauswahl inklusive automatischer Berechnung der EU-Sommerzeit (MEZ/MESZ Richtlinie).
    3. Benutzerkonto: Name, Passwort, Avatar und PC-Name.
    4. Datenträger-Auswahl: Automatische Erkennung und Formatierung von SATA-AHCI-Laufwerken.
    5. Fortschrittsanzeige: Installation von Systemdateien, Erstellung von CONFIG.DAT.
- Windows Vista Aero GUI Framework:
  - Runder Vista Start-Orb mit Glaseffekt und Windows-Logo.
  - Dynamisches Startmenü mit transparenter Applikations- und Systemspalte.
  - Windows-Suchleiste mit Text-Cursor, Horizontalschnitt und Scroll-Offset.
  - Fensterrahmen im typischen Aero-Glas-Look (blaue/dunkelgraue Farbverläufe, rote Close-Buttons).
- Vollständiges CP437 / ISO-8859-1 Zeichensatz-Font (font.c):
  - 256 Glyphen (inkl. ä, ö, ü, Ä, Ö, Ü, ß, §, °).
- Erweitertes Eingabe-Subsystem:
  - Unterstützung für Sondertasten: Windows/Super-Taste, Pfeiltasten, Pos1, Ende, Entf, Tab, F1.
  - Cursor-Formen: Standard-Pfeil, Zeigehand (Hover) und Text-I-Beam.
- Editor (Notepad) mit Word-Wrap:
  - Intelligenter Textumbruch an Wortgrenzen, dynamische Textcursor-Verwaltung.

## Verzeichnisstruktur
veloos/ \
├── setup.c/.h       # Windows Vista Setup Wizard & Config-Loader \
├── desktop.c/.h     # Aero Taskbar, Start-Orb & Startmenü \
├── wm.c/.h          # Aero Fensterrahmen & Word-Wrap Engine \
├── keyboard.c/.h    # Layout-Umschaltung (QWERTZ/QWERTY) \
├── font.c/.h        # 256 Zeichen Bitmap-Font \
├── kernel.c         # Bootstrapping & Hardware-Erkennung \
└── Makefile         # Optimierte Kompilierung (SSE/AVX abgesichert)

## Build & Ausführung
Befehl: make clean && make run
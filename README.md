![visitors](https://laobi.icu/badge?page_id=vntx-labs.visitor-badge&left_text=Total%20Visitors%3A&left_color=%231a5fb4&right_color=%231a5fb4&radius=10&height=25)
# VeloOS - Version 10.1.0 (Native Shell, Command Pipelines & QWERTZ Translation)

VeloOS V10.1.0 schließt die Brücke zwischen grafischer Desktop-Umgebung und professioneller Kommandozeile: Eine native Shell (`sh.c`), flexible Ein-/Ausgabeumleitungen, Befehlspipelines und fehlerfreie Tastaturübersetzung für Entwickler.

## Neuheiten & Meilensteine
- Native VeloOS Shell (apps/sh/sh.c -> SH.BIN):
  - Integrierte Eingabeaufforderung im Windows-Konsolenstil mit vollem Pfad-Prompt (`C:/Users/Desktop>`).
  - Umfangreicher Befehlssatz:
    - Dateisystem: `dir`/`ls`, `cd`, `pwd`, `type`/`cat`, `echo`, `touch`, `cp`/`copy`, `mv`/`move`, `ren`/`rename`, `md`/`mkdir`, `del`/`rm`.
    - System & Diagnose: `help`, `codes`, `whoami`, `hostname`, `uname`, `sysinfo`, `clear`/`cls`, `exit`.
    - Applikationsstarter: Direkter Start von `explorer`, `notepad` oder beliebigen `.BIN`-Dateien.
- Command Pipelines & I/O-Redirection:
  - Unterstützung für Befehlsketten mit Semikolon (`;`), logischem UND (`&&`) und logischem ODER (`||`).
  - Output-Redirection in Dateien: Truncate-Modus (`>`) und Append-Modus (`>>`).
  - Intelligente Tab-Vervollständigung (Tab-Completion) für Dateien und Ordner sowie Navigations-Historie (Pfeiltasten Up/Down).
- Offizielle VeloOS Codename-Engine (`codes`):
  - Anzeige und Abfrage der historischen VeloOS-Codenamen von V1 bis V10 (V10 = "Xenon").
- Robuste Tastatur-Layout-Engine (keyboard.c / keyboard.h):
  - Vollständige Entwirrung von UEFI-US-ASCII-Codes auf das deutsche QWERTZ-Layout.
  - Präzise Übertragung von Schrägstrichen (`/`, `\`), Doppelpunkten (`:`), Umlauten und Sonderzeichen für fehlerfreie Terminal- und Pfadeingaben.
- Desktop-Integration:
  - Aufnahme der Eingabeaufforderung direkt in das VeloOS Startmenü.

## Verzeichnisstruktur
veloos/ \
├── apps/sh/ \
│   └── sh.c         # Native VeloOS Shell (Pipelines, Redirection, Tab-Complete) \
├── keyboard.c/.h    # Vollständiges QWERTZ-Sonderzeichen- & Scancode-Mapping \
├── desktop.c        # Startmenü-Erweiterung (Eingabeaufforderung) \
├── syscall.c        # Userland-Execution & Konsolen-Event-Routing \
└── Makefile

## Build & Ausführung
Befehl: make clean && make run
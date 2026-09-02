![visitors](https://laobi.icu/badge?page_id=vntx-labs.visitor-badge&left_text=Total%20Visitors%3A&left_color=%231a5fb4&right_color=%231a5fb4&radius=10&height=25)
# VeloOS v1.0.0

**VeloOS: Fast, minimal, and resilient. A low-level 64-bit kernel project born from rapid prototyping and persistent debugging.**

---

## 🌌 Projekt-Übersicht

VeloOS v1.0.0 ist ein von Grund auf selbst entwickeltes, **hoch-modulares 64-Bit Bare-Metal-Betriebssystem**, das den nackten x86_64-Long-Mode direkt steuert. Das System verzichtet komplett auf bestehende Kernelstrukturen und implementiert eine eigene Low-Level-Architektur für maximale Performance.

### 🔥 Core-Features & Architektur-Meilensteine (v1.0.0)
* **Real-to-Protected-to-Long-Mode Transition:** Robuster Bootloader (`stage1.asm`, `stage2.asm`) mit aktiviertem A20-Gate, GDT-Setup, 1-GB-Identity-Mapping und Sprung in den 64-Bit Long Mode.
* **C-Kernel & Interrupt-Management:** Eigenständiger 64-Bit C-Kernel (`kernel.c`) mit PIC-Remapping, IDT-Registrierung und sicherer Interrupt Service Routine (ISR) für die Tastatur.
* **Robustes Tastatur-Subsystem:** Vollständiger PS/2-Tastaturtreiber (`keyboard.c`, `keyboard.h`) mit deutscher Keymap (Normal, Shift, AltGr), Extended-Mode-Unterstützung (0xE0) und Puffer-Bereinigung.
* **Persistent Disk VFS (Virtual File System):** Eigenes Sektor-basiertes Dateisystem (`mkfs.py`), das Anwendungen wie den Matrix-Screensaver dynamisch ab Sektor 41 speichert und zur Laufzeit lädt.
* **Isolierter Userspace-Stack:** Sichere Programmausführung bei `0x400000` mit getrenntem Userspace-Stack (`0x300000`), um den Kernel-Stack vor Überläufen zu schützen.
* **Matrix-Screensaver:** Visuelle Demo-Anwendung (`matrix.c`), die eigenständig im Long Mode läuft und per ESC-Taste beendet werden kann.

---

## 📜 Rechtliche Hinweise & Urheberrecht (Copyright Protection)

### ⚠️ STRENGER RECHTLICHER SCHUTZ – URHEBERRECHTSHINWEIS
**Copyright © 2026 by Vantix (vntx-labs). Alle Rechte vorbehalten.**

Dieses Betriebssystem-Repository, einschließlich aller Quellcodes (`.c`, `.h`), Assembler-Dateien, Skripte, Makefiles und Binärdaten unterliegt dem **strengen Schutz des internationalen Urheberrechts (Copyright Law)**.

* **Keine unautorisierte Vervielfältigung (No Derivates / No Cloning):** Es ist strikt untersagt, den Code dieses Projekts zu kopieren, zu klonen, in eigene Repositories zu forken oder unter anderem Namen zu veröffentlichen.
* **Keine kommerzielle Nutzung:** Jegliche kommerzielle Verwertung oder Nutzung in proprietären Systemen ist illegal.

---

## 🛠️ Build & Ausführung (Host-Setup)

### Voraussetzungen
```bash
sudo apt update
sudo apt install gcc binutils make nasm genisoimage qemu-system-x86 python3
```

### Compilation & Start
```bash
make clean && make run
```
*Das Makefile kompiliert Stage 1, Stage 2, den C-Kernel, das Tastatur-Subsystem sowie das Matrix-Program, baut das VFS via `mkfs.py` und startet QEMU.*

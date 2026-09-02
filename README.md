![visitors](https://laobi.icu/badge?page_id=vntx-labs.visitor-badge&left_text=Total%20Visitors%3A&left_color=%231a5fb4&right_color=%231a5fb4&radius=10&height=25)
# VeloOS v2.0.0

**VeloOS v2.0.0: UEFI-Native Evolution, AHCI SATA Storage, Advanced FAT32 & Hardware-Accelerated Window Management.**

---

## 🌌 Projekt-Übersicht

Mit VeloOS v2.0.0 vollzieht das Projekt den strategischen Sprung von klassischem BIOS-Boot zu einer **modernen UEFI-basierten Architektur**. Das System integriert einen vollständigen AHCI-SATA-Treiber, ein natives FAT32-Dateisystem, einen Bitmap-Font-Renderer und ein grafisches Window-Management-Framework (WM).

### 🔥 Core-Features & Architektur-Meilensteine (v2.0.0)
* **UEFI Boot-Integration:** Direkter Start in der UEFI-Umgebung mit Nutzung von EFI-Boot-Diensten und stabiler Hardware-Initialisierung.
* **AHCI SATA-Controller-Treiber (`ahci.c` / `ahci.h`):** Direkte Ansteuerung von SATA-Controllern, Laufwerks-Identifizierung und High-Performance-Sektor-Lese/Schreiboperationen (bis zu 32 Ports).
* **Vollständiger FAT32-Treiber (`fat32.c` / `fat32.h`):** Integriertes FAT32-Dateisystem inklusive automatischer Formatierungsroutine (`fat32_format`), Cluster-Ketten-Verwaltung, Verzeichnis-Parsing und Datei-E/A.
* **Grafisches Window-Management (WM & Desktop):** 
  * Analytische Vektor- und Anti-Aliasing-Primitives mit Subpixel-Blending (`alpha_blend`, Linien, Kreise, abgerundete Rechtecke mit Farbverläufen).
  * Vollständiges Fenstersystem mit Titel leisten, Fokus-Steuerung, Minimieren, Schließen und automatischem Caching.
* **Erweiterter Bitmap-Font (`font.c` / `font.h`):** Vollständiger 8x16-Bitmap-Zeichensatz für pixelgenaue Textdarstellung in grafischen Fenstern und Oberflächen.
* **Anwendungs-Framework (Calc & Matrix):** Integrierte Anwendungen wie ein Rechner (`calc.c`) und eine grafische Matrix-Visualisierung.
* **Erweitertes Polling-Keyboard (`keyboard.c` / `keyboard.h`):** PS/2 i8042 Polling-Treiber mit robuster Entprellung und Unterstützung für den Betrieb nach `ExitBootServices()`.

---

## 📜 Rechtliche Hinweise & Urheberrecht (Copyright Protection)

### ⚠️ STRENGER RECHTLICHER SCHUTZ – URHEBERRECHTSHINWEIS
**Copyright © 2026 by Vantix (vntx-labs). Alle Rechte vorbehalten.**

Dieses Repository unterliegt dem strengen Schutz des internationalen Urheberrechts. Jegliche unautorisierte Vervielfältigung, kommerzielle Nutzung oder eigenständige Distribution ist strikt untersagt.

---

## 🛠️ Build & Ausführung

### Voraussetzungen
UEFI-Toolchain, GCC, Binutils und QEMU mit AHCI-Unterstützung.

### Start
```bash
make clean && make run
```
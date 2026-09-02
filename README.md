![visitors](https://laobi.icu/badge?page_id=vntx-labs.visitor-badge&left_text=Total%20Visitors%3A&left_color=%231a5fb4&right_color=%231a5fb4&radius=10&height=25)
# VeloOS - Version 2.0.0 (UEFI Native, AHCI SATA & FAT32)

VeloOS V2.0.0 vollzieht den technologischen Paradigmenwechsel: Abkehr vom veralteten 16-Bit BIOS hin zum modernen UEFI-Standard mit nativer Hardware-Grafik (GOP), echtem SATA-AHCI-Controller-Zugriff und vollständiger FAT32-Dateisystemunterstützung.

## Neuheiten & Meilensteine
- UEFI Boot-Architektur:
  - Kompiliert als 64-Bit PE32+ Binary (BOOTX64.EFI) über GNU-EFI.
  - Sichere Speicherallokation und sauberer Übergang mit ExitBootServices().
- GOP Hi-Res Grafik-Engine:
  - Native 32-Bit Framebuffer-Unterstützung (BGR/RGB).
  - Software-Double-Buffering (g_backbuffer) mit Pitch-/Scanline-Blit zur Vermeidung von Bildversatz.
  - Vollständiger 8x16 Bitmap-Font (font.c) für formatierte Textausgabe.
- SATA AHCI Treiber (ahci.c):
  - Vollständiger PCI-Bus-Scan (Bus 0-255, Device 0-31, Function 0-7) nach Speichercontrollern (Klasse 0x01).
  - Konfiguration der HBA Memory Space Register (ABAR BAR5), Port-Initialisierung, Command Lists & PRDT-DMA-Transfers.
  - Automatische Erkennung und IDENTIFY DRIVE-Abfrage für SATA-Platten.
- Vollwertiger FAT32-Treiber (fat32.c):
  - Boot Record (BPB) Parser, Cluster-Chain-Traversal und dynamische Cluster-Allokation.
  - Integrierte bare-metal Formatierfunktion (fat32_format) zur Laufzeit.
  - Lesen und Schreiben regulärer Dateien über Verzeichniseinträge.
- Persistentes Benutzer-Setup:
  - Dateibasierte Account-Verwaltung auf Festplatte (ACCOUNT.DAT) inklusive XOR-Verschlüsselung.
  - Login-Screen und grafischer Account-Erstellungs-Assistent.
- Desktop Environment V1 (desktop.c / wm.c):
  - Lineare mathematische Farbverläufe und zentriertes Vektor-Logo mit analytischem Anti-Aliasing (Subpixel-Blending).
  - Fenster-Manager mit Fenstertiteln, Close-Buttons und Fokus-Wechsel (Tab).
  - CMOS-RTC-Echtzeituhr in der Taskleiste.

## Verzeichnisstruktur
veloos/ \
├── kernel.c         # UEFI Main & Core-Subsysteme \
├── ahci.c/.h        # PCI AHCI SATA Treiber (DMA) \
├── fat32.c/.h       # FAT32 Lese-/Schreib-/Formatiertreiber \
├── keyboard.c/.h    # Non-blocking PS/2 Keyboard Poller \
├── font.c/.h        # 8x16 Raster-Font \
├── desktop.c/.h     # Grafische Desktop-Oberfläche \
├── wm.c/.h          # Window Manager & 2D AA-Primitives \
├── calc.c           # Bare-Metal Rechner mit Syntaxbaum \
└── Makefile         # UEFI Image Builder (os.img, data.img) \

## Build & Ausführung
Voraussetzungen: gcc, gnu-efi, mtools, dosfstools, qemu-system-x86, ovmf.

Befehl: make clean && make run
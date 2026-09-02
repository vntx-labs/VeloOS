![visitors](https://laobi.icu/badge?page_id=vntx-labs.visitor-badge&left_text=Total%20Visitors%3A&left_color=%231a5fb4&right_color=%231a5fb4&radius=10&height=25)
# VeloOS - Version 1.0.0 (Legacy BIOS MBR & ATA-PIO Core)

VeloOS V1.0.0 ist das Fundament des Betriebssystems. Es implementiert einen klassischen zweistufigen Bootloader (x86 Real Mode nach x86_64 Long Mode) ohne Fremd-Libraries und steuert die Hardware direkt über I/O-Ports und BIOS-Interrupts.

## Kernmerkmale dieser Version
- 2-Stage MBR Bootloader:
  - stage1.asm: 16-Bit Real Mode Bootsektor (0x7C00), aktiviert die A20-Gate-Linie über Port 0x92 und lädt Stage 2 sektorenweise via BIOS INT 0x13.
  - stage2.asm: Richtet die GDT ein, schaltet in den 32-Bit Protected Mode, initialisiert ein 1-GB-Identity-Paging (PML4 -> PDPT Huge Page bei 0x20000) und führt den Far-Jump in das 64-Bit-Codesegment durch.
- 64-Bit Bare-Metal C-Kernel:
  - Direkter VGA-Textmodus-Pufferzugriff bei 0xB8000 (80x25 Zeichen).
  - Isolierter Programm-Stack (0x300000), um Kernel-Stabilität bei Subprozessen zu sichern.
- ATA-Festplattentreiber (PIO-Modus):
  - Direkte Port-I/O-Ansteuerung über Primär-Kanal (0x1F0-0x1F7).
  - Polling über Statusregister (BSY- und DRQ-Bits).
- Proprietäres VFS (Virtual File System):
  - Generiert durch mkfs.py: Schreibt Inodes ab Sektor 40 (32 Byte Name, 4 Byte Start-LBA, 4 Byte Größe).
- Interaktive Text-Shell:
  - Befehle: help, clear, shutdown (APM/ACPI Port 0x604), reboot (8042 Reset Port 0x64), run <datei>.
  - Ausführung eigenständiger Binärprogramme (z. B. matrix.bin geladen an 0x400000).
- PS/2 Tastaturtreiber:
  - PIC-Remapping (Master: 0x20, Slave: 0x28), IDT-Interrupt-Gate für IRQ 1 (Vektor 0x21), deutsches QWERTZ-Scancode-Mapping mit Shift & AltGr.

## Verzeichnisstruktur
veloos/ \
├── stage1.asm       # MBR Bootsektor \
├── stage2.asm       # Protected- & Long-Mode Switch \
├── kernel.c         # 64-Bit Kernel, Shell & ATA-Treiber \
├── keyboard.c/.h    # PS/2 Interrupt-Treiber & Keymaps \
├── matrix.c/.ld     # Eigenständige Test-Applikation \
├── linker.ld        # Kernel-Linkerskript (Basis 0x7E00) \
├── mkfs.py          # VFS-Erstellungsskript \
└── Makefile         # Build-System

## Build & Ausführung
Voraussetzungen: nasm, gcc, binutils, python3, qemu-system-x86.

Befehle:
make clean
make run
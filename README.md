![visitors](https://laobi.icu/badge?page_id=vntx-labs.visitor-badge&left_text=Total%20Visitors%3A&left_color=%231a5fb4&right_color=%231a5fb4&radius=10&height=25)
# VeloOS - Version 3.0.0 (Intel e1000 Gigabit, i8042 Mouse & Syscalls)

Mit VeloOS V3.0.0 wird das Betriebssystem um interaktive Hardware-Mausunterstützung, eine Dirty-Rectangle-Grafik-Engine, einen integrierten Intel Gigabit-Netzwerktreiber sowie eine moderne x86-64 Fast-Syscall-Schnittstelle erweitert.

## Neuheiten & Meilensteine
- Intel 82540EM (e1000) Netzwerktreiber (net.c):
  - PCI-Scanner nach e1000-Geräten, Aktivierung von Bus-Mastering und Memory-Mapped I/O (MMIO).
  - Circular Descriptor Rings für TX (Senden) und RX (Empfangen).
  - Implementierter DHCP-Client (Discover/Request) für dynamische IP-Zuweisung.
  - Integrierter TCP/IP-Stack mit HTTP-Client: Abfrage von Standort- und Zeitzonendaten via IP-API.
- Hardware-Mausunterstützung (mouse.c):
  - Aktivierung des PS/2 Auxiliary-Ports über den i8042-Tastaturcontroller.
  - 3-Byte-Paket-Dekodierung, Interrupt/Polling-Entzerrung zwischen Tastatur und Maus.
  - 12x19 High-Contrast Mauszeiger, Klick-Events und Drag & Drop für Desktop-Fenster.
- Performance Grafik-Pipeline:
  - Dirty Rectangle Engine: Berechnet minimale Neuzeichnungs-Rechtecke (wm_mark_dirty), um Bandbreite zu sparen.
  - SSE2 Hardware Blitter: 128-Bit Vektor-Befehle (movdqu) im Framebuffer-Blit für stabile 60 FPS.
  - 1080p Wallpaper Loader: Liest WALL.BIN direkt von FAT32 und skaliert es hardwarenah auf Bildschirmgröße.
- x86-64 Fast-Syscall Subsystem (syscall.c):
  - Konfiguration der Model-Specific Registers (MSR_EFER, MSR_STAR, MSR_LSTAR, MSR_FMASK).
  - Hardwarenahe Umschaltung über syscall / sysretq.
  - Dispatcher für Fenstererstellung, Rendering, Event-Polling und Programm-Beendigung.
- Taschenrechner-Applikation (calc.c):
  - Modulares UI-Fenster mit interaktiven Schaltflächen, LCD-Display und Punkt-vor-Strich-Arithmetik.

## Verzeichnisstruktur
veloos/ \
├── kernel.c         # Kernel Entry & Event-Loop \
├── mouse.c/.h       # i8042 PS/2 & USB Maus-Treiber \
├── net.c/.h         # Intel e1000 Treiber, DHCP & HTTP \
├── syscall.c/.h     # MSR Syscall Bridge & ABI \
├── user.h           # Header für Userspace-Programme \
├── calc.c/.h        # Grafischer Taschenrechner \
├── desktop.c/.h     # Desktop, Statusleiste & RTC \
├── wm.c/.h          # Window Manager mit Drag & Drop \
├── gen_wallpaper.py # Wallpaper-Generator (WALL.BIN) \
└── Makefile         # Multi-Drive UEFI Build-Pipeline

## Build & Ausführung
Befehl: make clean && make run
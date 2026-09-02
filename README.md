![visitors](https://laobi.icu/badge?page_id=vntx-labs.visitor-badge&left_text=Total%20Visitors%3A&left_color=%231a5fb4&right_color=%231a5fb4&radius=10&height=25)
# VeloOS - Version 8.0.0 (PCIe NVMe SSD Driver & Universal Bare-Metal Boot)

Mit Version 8.0.0 verlässt VeloOS die reine Emulation und erreicht volle Bare-Metal-Fähigkeit auf moderner x86_64-Hardware: Integrierter NVMe-SSD-Treiber sowie gebrauchsfertige Images für Ventoy und reale USB-Sticks.

## Neuheiten & Meilensteine
- Nativer PCIe NVMe SSD Treiber (nvme.c):
  - Scan des PCI-Konfigurationsraums nach NVMe-Controllern (Klasse 0x01, Subklasse 0x08, ProgIF 0x02).
  - Allokation und Verwaltung von Contiguous Admin Submission & Completion Queues (ASQ / ACQ).
  - Initialisierung von I/O Queues (IOSQ / IOCQ) und Ansteuerung der Doorbell-Register.
  - Sektorweises Lesen und Schreiben (NVME_NVM_CMD_READ / NVME_NVM_CMD_WRITE) mit bis zu 4 KB DMA-Puffern.
- Multi-Storage-Fallback-Architektur (ahci.c):
  - Erkennt parallel klassische SATA-AHCI-Controller und moderne M.2 NVMe SSDs.
  - Automatische Bereitstellung des primären Laufwerks als Systemlaufwerk C:.
- Universeller Bare-Metal & Ventoy Boot:
  - make ventoy: Erstellt ein partioniertes Disk-Image (velo.img) mit MBR Type 0xEF (EFI System Partition), das direkt auf Ventoy-Sticks kopiert werden kann.
  - make iso: Erstellt eine vollwertige Hybrid-UEFI-Boot-ISO (velo.iso via xorriso).
  - make flash TARGET=/dev/sdX: Schreibt das System direkt auf einen physischen USB-Stick.
- Robuste Hardware-Absicherung:
  - Non-blocking KBC-Handling, um Tastatur- und Mausblockaden bei variablen Taktraten zu verhindern.
  - Dual-Polling über EFI Simple Pointer Protocol und PS/2-Hardware-Fallback.

## Verzeichnisstruktur
veloos/ \
├── nvme.c/.h        # Nativer PCIe NVMe SSD Treiber \
├── ahci.c/.h        # SATA & NVMe Multi-Controller Dispatcher \
├── kernel.c         # Bootstrapping & Hardware-Erkennung \
├── Makefile         # Ventoy-, ISO- & USB-Flash-Ziele \
└── ...

## Build & Installation auf echter Hardware
A. Für Ventoy: \
make ventoy \
(Kopiere die erstellte Datei velo.img oder make iso -> velo.iso auf deinen Ventoy-USB-Stick.)

B. Direktes Flashen auf USB-Stick: \
make flash TARGET=/dev/sdX
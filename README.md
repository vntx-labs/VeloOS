![visitors](https://laobi.icu/badge?page_id=vntx-labs.visitor-badge&left_text=Total%20Visitors%3A&left_color=%231a5fb4&right_color=%231a5fb4&radius=10&height=25)
# VeloOS

**VeloOS: Fast, minimal, and resilient. A low-level kernel project born from rapid prototyping and persistent debugging. Part of the 'Velo' ecosystem—engineered for performance and open-source collaboration.**

---

## 🌌 Projekt-Übersicht

VeloOS ist ein von Grund auf selbst entwickeltes, **hoch-modulares 64-Bit Bare-Metal-Betriebssystem**, das den nackten x86_64-Long-Mode direkt außerhalb der UEFI-Umgebung kontrolliert. Das System verzichtet komplett auf bestehende Linux- oder Windows-Kernelstrukturen und setzt auf eine vertikal integrierte Grafik- und Input-Pipeline für maximale Performance bei absoluter Null-Latenz.

### 🔥 Core-Features & Architektur-Meilensteine
* **Präemptives Multitasking:** Ein hardwarenaher Scheduler in `sched.c`, der Threads (wie den GUI-Compositor und die Netzwerk-Services) über den APIC-Timer-Interrupt (100 Hz Ticks) im fliegenden Wechsel synchronisiert.
* **Windows Vista Aero GUI Framework:** Ein ultraschnelles 2D-Rendering-System im Ring 0/3 mit dynamischer Laufzeit-Skalierung (Resolution Switching). Berechnet mathematische Farbverläufe, Kanten-Radius-Spans und bitweises, divisionsfreies Parallel-Alpha-Blending direkt im Triple-Buffer-Backbuffer.
* **Eigene Standard-Bibliothek (VeloLIBC):** Eine proprietäre, POSIX-ähnliche `libc`, die die Brücke zwischen hardwaregeschütztem Ring-3-Userland und dem Kernel über den modernen `syscall`/`sysret`-Mechanismus schlägt.
* **Persistent Storage (AHCI & FAT32):** Eigene native Sektor-Treiber für SATA-Festplatten, die Partitionsdaten fehlerfrei über den AHCI-Controller einlesen und beschreiben.
* **Netzwerk- & Krypto-Stack:** Ein integrierter Intel e1000 Gigabit-Netzwerktreiber mit DHCP-Lease und funktionierendem HTTPS/TLS-Handshake im Kernel für sichere, verschlüsselte Live-Abfragen.
* **0-Delay CMOS RTC:** Sekundengenaue Hardware-Zeitsynchronisation direkt über die I/O-Ports des Mainboard-CMOS-Chips inklusive dynamischer Sommer-/Winterzeit-RAM-Lookup-Tabellen.


---

## 📜 Rechtliche Hinweise & Urheberrecht (Copyright Protection)

### ⚠️ STRENGER RECHTLICHER SCHUTZ – URHEBERRECHTSHINWEIS
**Copyright © 2026 by Vantix (vntx-labs). Alle Rechte vorbehalten.**

Dieses Betriebssystem-Repository, einschließlich aller Quellcodes (`.c`, `.h`), Assembler-Dateien, Skripte, Makefiles, Binärdaten und visuellen Assets (Vantix-Logo-Schwung, optimierte Ordner-Icons), unterliegt dem **strengen Schutz des internationalen Urheberrechts (Copyright Law)**.

* **Keine unautorisierte Vervielfältigung (No Derivates / No Cloning):** Es ist strikt untersagt, den Code dieses Projekts zu kopieren, zu klonen, in eigene Repositories zu forken (außer zum Zweck von Pull Requests an dieses Upstream-Projekt), unter anderem Namen zu veröffentlichen oder Teile davon in andere Projekte einzubauen.
* **Keine kommerzielle Nutzung:** Jegliche kommerzielle Verwertung, Nutzung in proprietären Systemen oder der Verkauf von Binär-Images (`.efi`, `.img`), die auf diesem Code basieren, ist illegal und wird rechtlich verfolgt.
* **Anonymitätsschutz:** Die Identität des Kern-Entwicklers (**Vantix / vntx-labs**) ist im digitalen Raum vollständig isoliert und geschützt. Jegliche Versuche, diese Online-Identität mit realen Identitäten zu verknüpfen, verletzen die Privatsphäre und haben rechtliche Konsequenzen.

### 🤝 Bestimmungen zur Mitarbeit (Contribution Policy)
Wie in der beiliegenden `LICENSE` definiert, ist eine **Mitarbeit und Code-Kooperation ausdrücklich erlaubt und erwünscht**, solange sie unter folgenden Bedingungen stattfindet:

1. **Pull Requests:** Code-Verbesserungen, Bugfixes (z. B. beim FAT32-Unterverzeichnis-Routing) und Feature-Erweiterungen müssen über offizielle Pull Requests eingereicht werden.
2. **Rechteübertragung:** Mit dem Einreichen eines Pull Requests oder Beitrags stimmst du zu, dass dein bereitgestellter Code automatisch Teil des geschützten VeloOS-Ökosystems wird und den gleichen strengen Urheberrechtsbestimmungen von Vantix unterliegt.
3. **Open-Source-Erhalt:** Das Projekt bleibt als kollaboratives Low-Level-Meisterwerk sichtbar, ist aber vor Diebstahl und unautorisierten Forks geschützt.

---

## 🛠️ Build & Ausführung (Host-Setup)

Um VeloOS mit nativer Hardware-Geschwindigkeit und absolutem 0-Lag auf deinem Linux Mint-System zu emulieren, wird ein Prozessor-Passthrough benötigt.

### Voraussetzungen
```bash
sudo apt update
sudo apt install gcc binutils make mtools qemu-system-x86
```

### Compilation & Start
```bash
make clean && make run
```
*Das Makefile baut automatisch die `velolibc.a`, kompiliert die Ring-3-Apps (`EXPLORER.BIN`) und startet QEMU mit aktivierter KVM-Hardwarebeschleunigung (`-cpu host`).*


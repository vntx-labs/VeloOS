// kernel.c - 64-Bit OS Kernel mit echtem ATA-Festplatten-VFS
#include <stdint.h>
#include "keyboard.h" // Bindet das neue, sichere Tastatur-Subsystem ein

// VGA Textmodus-Speicher (Global für Treiber zugänglich)
volatile unsigned char* video_memory;
int cursor_position;

// Shell-Zustandstracker (Global für Tastatur-Interrupt-Eingaben)
char command_buffer[64];
int command_length;

// Speicheradresse für den isolierten Programm-Stack (3 MB)
#define USER_STACK_ADDRESS 0x300000

// Globales Backup für den Stack-Pointer während der Programmausführung
uint64_t kernel_stack_backup = 0;

// =====================================================================
// EFFIZIENTE HARDWARE PORT-I/O FUNKTIONEN
// =====================================================================
static inline unsigned char inb(unsigned short port) {
    unsigned char result;
    __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outb(unsigned short port, unsigned char data) {
    __asm__ volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}

static inline void outw(unsigned short port, unsigned short data) {
    __asm__ volatile("outw %0, %1" : : "a"(data), "Nd"(port));
}

static inline void insw(unsigned short port, void* addr, unsigned int count) {
    __asm__ volatile("cld; rep insw" : "+D"(addr), "+c"(count) : "d"(port) : "memory");
}

// =====================================================================
// NATIV-DYNAMISCHER ATA-FESTPLATTENTREIBER
// =====================================================================
void ata_read_sector(uint32_t lba, uint8_t* buffer) {
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F)); // Master-Laufwerk auswählen
    outb(0x1F2, 1);                           // 1 Sektor anfordern
    outb(0x1F3, (uint8_t)lba);                // LBA Bits 0-7
    outb(0x1F4, (uint8_t)(lba >> 8));         // LBA Bits 8-15
    outb(0x1F5, (uint8_t)(lba >> 16));        // LBA Bits 16-23
    outb(0x1F7, 0x20);                        // Befehl: "Read Sectors" (0x20)

    // Polling: Warten bis BUSY (0x80) weg und DRQ (0x08) da ist
    while ((inb(0x1F7) & 0x80) || !(inb(0x1F7) & 0x08));

    // 256 Wörter (512 Bytes) direkt in den Puffer streamen
    insw(0x1F0, buffer, 256);
}

// =====================================================================
// UNBEGRENZTES PERSISTENTES VFS (VIRTUAL FILE SYSTEM)
// =====================================================================
#define SECTOR_ROOT_DIR 40 // Sektor 40 enthält das Root-Verzeichnis
#define MAX_FILENAME 32

typedef struct {
    char name[MAX_FILENAME];
    uint32_t start_sector;
    uint32_t size;
} __attribute__((packed)) disk_inode_t;

// Bare-Metal String-Vergleichsfunktion
int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

disk_inode_t vfs_open(const char* filename) {
    uint8_t sector_buffer[512];
    disk_inode_t found_file;
    found_file.start_sector = 0; // 0 bedeutet "Nicht gefunden"

    // Root-Verzeichnis-Sektor live von der Festplatte lesen
    ata_read_sector(SECTOR_ROOT_DIR, sector_buffer);

    // Bis zu 12 Inodes pro Sektor prüfen (12 * 40 Bytes = 480 Bytes)
    disk_inode_t* inodes = (disk_inode_t*)sector_buffer;

    for (int i = 0; i < 12; i++) {
        if (inodes[i].start_sector == 0) continue;

        if (strcmp(filename, inodes[i].name) == 0) {
            for (int k = 0; k < MAX_FILENAME; k++) found_file.name[k] = inodes[i].name[k];
            found_file.start_sector = inodes[i].start_sector;
            found_file.size = inodes[i].size;
            return found_file;
        }
    }
    return found_file;
}

void print_string(const char* str, unsigned char color) {
    for (int i = 0; str[i] != '\0'; i++) {
        video_memory[cursor_position] = str[i];
        video_memory[cursor_position + 1] = color;
        cursor_position += 2;
    }
}

// =====================================================================
// CORE-COMMAND INTERPRETER (SHELL)
// =====================================================================
void execute_command() {
    if (command_length == 0) return;
    command_buffer[command_length] = '\0';

    cursor_position = ((cursor_position / 160) + 1) * 160; // Neue Zeile

    // Befehl: "clear"
    if (strcmp(command_buffer, "clear") == 0) {
        for (int i = 0; i < 80 * 25 * 2; i += 2) {
            video_memory[i] = ' ';
            video_memory[i + 1] = 0x07;
        }
        cursor_position = 0;
        print_string("Bildschirm geleert.", 0x0E);
    }
    // Befehl: "help"
    else if (strcmp(command_buffer, "help") == 0) {
        print_string("Befehle: help, clear, run <datei>, shutdown, reboot", 0x0E);
    }
    // Befehl: "shutdown" (ACPI Poweroff)
    else if (strcmp(command_buffer, "shutdown") == 0) {
        print_string("Fahre System herunter...", 0x0C);
        outw(0x604, 0x2000); 
    }
    // Befehl: "reboot" (Echter System-Neustart)
    else if (strcmp(command_buffer, "reboot") == 0) {
        print_string("Fuehre echten System-Reboot durch...", 0x0E);
        outb(0x64, 0xFE);
        __asm__ volatile ("cli; lidt 0; int $3");
        while(1);
    }
    // Befehl: "run <datei>" (Echtes VFS Streaming & Ausführung mit isoliertem Stack)
    else if (command_buffer[0] == 'r' && command_buffer[1] == 'u' &&
             command_buffer[2] == 'n' && command_buffer[3] == ' ') {
        char* filename = &command_buffer[4];

        // Datei über das persistente Festplatten-VFS suchen!
        disk_inode_t file = vfs_open(filename);

        if (file.start_sector != 0) {
            print_string("Lade Programm aus VFS...", 0x0A);

            // Speicherbereich ab 0x400000 (4 MB) als Programm-Zone nutzen
            uint8_t* execute_buffer = (uint8_t*)0x400000;
            uint32_t sectors_to_read = (file.size + 511) / 512;

            // Sektoren blockweise via temporärem Buffer bytegenau kopieren
            for (uint32_t s = 0; s < sectors_to_read; s++) {
                uint8_t sector_tmp[512];
                ata_read_sector(file.start_sector + s, sector_tmp);
                for (int b = 0; b < 512; b++) {
                    execute_buffer[(s * 512) + b] = sector_tmp[b];
                }
            }

            // Ausführung auf dem isolierten Userspace-Stack (Sichert den Kernel-Stack ab)
            __asm__ volatile (
                "movq %%rsp, %2\n\t"       // Alten Kernel-Stack-Pointer sichern
                "movq %1, %%rsp\n\t"       // Neuen Programm-Stack laden
                "movq %1, %%rbp\n\t"       // Base-Pointer anpassen
                "call *%0\n\t"             // Programm bei 0x400000 aufrufen
                "movq %2, %%rsp\n\t"       // Ursprünglichen Kernel-Stack wiederherstellen
                "movq %%rsp, %%rbp\n\t"    // Kernel-Base-Pointer wiederherstellen
                :
                : "r"((void*)0x400000), "r"((uint64_t)USER_STACK_ADDRESS), "m"(kernel_stack_backup)
                : "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "memory"
            );

            print_string("Programm erfolgreich beendet.", 0x0A);
        } else {
            print_string("Datei nicht im VFS-Sektor gefunden!", 0x0C);
        }
    }
    else {
        print_string("Unbekannter Befehl: ", 0x0C);
        print_string(command_buffer, 0x0C);
    }

    cursor_position = ((cursor_position / 160) + 1) * 160;
    print_string("> ", 0x0F);
    command_length = 0;
}

// =====================================================================
// KERNEL MAIN ENTRY (URSALYSE RUNTIME INITIALIZER)
// =====================================================================
__attribute__((used))
void kernel_main() {
    // Variablen-Initialisierung & BIOS Müll bügeln
    video_memory = (volatile unsigned char*)0xb8000;
    cursor_position = 480; 
    command_length = 0; 

    // Start-Prompt anzeigen
    print_string("C-Kernel mit ATA-Festplatten VFS aktiv!", 0x0A);
    cursor_position = ((cursor_position / 160) + 1) * 160;
    print_string("> ", 0x0F);

    // Initialisiert den PIC, registriert die ISR in der IDT und führt "sti" aus
    init_keyboard();

    // Reiner Unix-Stil: CPU schläft stromsparend, bis ein Interrupt (Tastendruck) sie weckt
    while (1) {
        __asm__ volatile("hlt");
    }
}
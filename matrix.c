// matrix.c - Eigenstaendiges Matrix-Programm fuer das Festplatten-VFS

volatile unsigned char* video_memory = (volatile unsigned char*)0xb8000;

// GLOBALE VARIABLE: Verhindert Stack-Overflows im freistehenden Kernel-Modus!
int drop_y[80];

// Vorwärtsdeklarationen für die Hardware-Ports
static inline unsigned char inb(unsigned short port);
static inline void outb(unsigned short port, unsigned char data);

// ZWINGEND GANZ OBEN: Damit diese Funktion exakt bei 0x400000 startet!
void _start() {
    // 1. Tastaturpuffer einmalig komplett leer raeumen
    while (inb(0x64) & 0x01) {
        inb(0x60);
    }

    // 2. Bildschirm komplett schwaerzen
    for (int i = 0; i < 80 * 25 * 2; i += 2) {
        video_memory[i] = ' ';
        video_memory[i + 1] = 0x00;
    }

    // Initialisierung des globalen Arrays
    unsigned int pseudo_rand = 0x42E1u;
    for (int col = 0; col < 80; col++) {
        pseudo_rand = (pseudo_rand >> 1) ^ (-(pseudo_rand & 1u) & 0xB400u);
        drop_y[col] = -(pseudo_rand % 30);
    }

    // Animationsschleife (bricht bei ESC ab)
    while (1) {
        if (inb(0x64) & 0x01) {
            unsigned char scancode = inb(0x60);
            if (scancode == 0x01) { // ESC Scancode
                break;
            }
        }

        for (int col = 0; col < 80; col++) {
            int current_y = drop_y[col];

            // Kopf zeichnen
            if (current_y >= 0 && current_y < 25) {
                int pos = (current_y * 80 + col) * 2;
                pseudo_rand = (pseudo_rand >> 1) ^ (-(pseudo_rand & 1u) & 0xB400u);
                char random_char = 33 + (pseudo_rand % 93);

                video_memory[pos] = random_char;
                video_memory[pos + 1] = 0x0F; // Weiss
            }

            // Schweif faerben
            if (current_y - 1 >= 0 && current_y - 1 < 25) {
                int pos_above = ((current_y - 1) * 80 + col) * 2;
                video_memory[pos_above + 1] = 0x0A; // Helles Gruen
            }

            // Ausblenden
            if (current_y - 8 >= 0 && current_y - 8 < 25) {
                int pos_fade = ((current_y - 8) * 80 + col) * 2;
                video_memory[pos_fade + 1] = 0x02; // Dunkelgruen
            }
            if (current_y - 12 >= 0 && current_y - 12 < 25) {
                int pos_black = ((current_y - 12) * 80 + col) * 2;
                video_memory[pos_black] = ' ';
                video_memory[pos_black + 1] = 0x00;
            }

            drop_y[col]++;

            if (drop_y[col] >= 35) {
                pseudo_rand = (pseudo_rand >> 1) ^ (-(pseudo_rand & 1u) & 0xB400u);
                drop_y[col] = -(pseudo_rand % 15);
            }
        }

        // Korrekte Takt-Bremse via outb an Port 0x80
        for (int repeat = 0; repeat < 10; repeat++) {
            for (volatile int delay = 0; delay < 15000; delay++) {
                outb(0x80, 0);
            }
        }
    }

    // Puffer leeren vor der Rueckkehr zur Shell
    while (inb(0x64) & 0x01) {
        inb(0x60);
    }

    // Bildschirm zuruecksetzen auf Standard-Terminal-Farben
    for (int i = 0; i < 80 * 25 * 2; i += 2) {
        video_memory[i] = ' ';
        video_memory[i + 1] = 0x07;
    }
}

// Hardware-Helfer unterhalb von _start platzieren
static inline unsigned char inb(unsigned short port) {
    unsigned char result;
    __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outb(unsigned short port, unsigned char data) {
    __asm__ volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}

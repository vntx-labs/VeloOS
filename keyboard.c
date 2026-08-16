#include "keyboard.h"

// Ausgelagerte Hardware-Port-Funktionen
static inline unsigned char inb(unsigned short port) {
    unsigned char result;
    __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outb(unsigned short port, unsigned char data) {
    __asm__ volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}

// Ausgelagerte Zustandstracker
static int shift_pressed = 0;
static int altgr_pressed = 0;
static int extended_mode = 0;

// Externe Kernel-Variablen & Funktionen einbinden
extern volatile unsigned char* video_memory;
extern int cursor_position;
extern char command_buffer[64];
extern int command_length;
extern void execute_command();
extern void print_string(const char* str, unsigned char color);

// Deutsche Keymaps
const char keymap_normal[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5', 
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0', 
    [0x0C] = '-', [0x0D] = '`', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't', 
    [0x15] = 'z', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p', 
    [0x1A] = '[', [0x1B] = ']', [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g', 
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = ';', 
    [0x28] = '\'', [0x29] = '^', 
    [0x2B] = '#', [0x2C] = 'y', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', 
    [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.', 
    [0x35] = '-', [0x37] = '*', [0x39] = ' ', [0x56] = '<'
};

const char keymap_shift[128] = {
    [0x02] = '!', [0x03] = '"', [0x05] = '$', [0x06] = '%', 
    [0x07] = '&', [0x08] = '/', [0x09] = '(', [0x0A] = ')', [0x0B] = '=', 
    [0x0C] = '?', [0x0D] = '`', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T', 
    [0x15] = 'Z', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P', 
    [0x1A] = '{', [0x1B] = '}', [0x1C] = '\n',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G', 
    [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L', [0x27] = ':', 
    [0x28] = '@', [0x2B] = '\'', [0x2C] = 'Y', [0x2D] = 'X', [0x2E] = 'C', 
    [0x2F] = 'V', [0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = '<', 
    [0x34] = '>', [0x35] = '_', [0x37] = '*', [0x39] = ' ', [0x56] = '>'
};

const char keymap_altgr[128] = {
    [0x10] = '@', 
    [0x12] = ']', 
    [0x08] = '{', 
    [0x09] = '[', 
    [0x0A] = ']', 
    [0x0B] = '}', 
    [0x0C] = '\\',
    [0x1B] = '~'  
};

// IDT-64 Bit Gate Struktur
typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  types_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed)) idt_entry_64_t;

idt_entry_64_t* idt = (idt_entry_64_t*)0x1000;
extern void keyboard_isr_wrapper(); 

void init_keyboard() {
    // Alten Restmüll aus dem Tastaturpuffer lesen, damit er nicht blockiert
    while (inb(0x64) & 0x01) {
        inb(0x60);
    }

    // 1. PIC Remap (IRQs über Exceptions schieben)
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0xFD); outb(0xA1, 0xFF); // Nur IRQ 1 (Keyboard) aktivieren auf Master

    // 2. IDT-Eintrag für IRQ 1 (Vektor 0x21) setzen
    uint64_t addr = (uint64_t)keyboard_isr_wrapper;
    idt[0x21].offset_low  = (uint16_t)(addr & 0xFFFF);
    idt[0x21].selector    = 0x18; // 64-Bit Code-Segment
    idt[0x21].ist         = 0;
    idt[0x21].types_attr  = 0x8E; // Present, Ring 0, Interrupt Gate
    idt[0x21].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
    idt[0x21].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt[0x21].reserved    = 0;

    // 3. CPU Interrupts freischalten
    __asm__ volatile("sti");
}

// Der echte Interrupt Handler mit Hardware-Absicherung
__attribute__((used))
void keyboard_interrupt_handler() {
    // Prüfen, ob der Controller wirklich ein Byte bereithält (Bit 0 von Port 0x64)
    unsigned char status = inb(0x64);
    if (!(status & 0x01)) {
        goto end; // Wenn kein echtes Byte da ist, direkt abbrechen
    }

    unsigned char scancode = inb(0x60);

    // Extended Mode Byte abfangen
    if (scancode == 0xE0) { 
        extended_mode = 1; 
        goto end; 
    }

    // AltGr (Rechte Alt-Taste) Make- und Break-Codes verarbeiten
    if (extended_mode && scancode == 0x38) { 
        altgr_pressed = 1;
        extended_mode = 0;
        goto end;
    }
    if (extended_mode && scancode == 0xB8) { 
        altgr_pressed = 0;
        extended_mode = 0;
        goto end;
    }

    // Normale Shift-Tasten abfangen
    if (scancode == 0x2A || scancode == 0x36) { shift_pressed = 1; extended_mode = 0; goto end; }
    if (scancode == 0xAA || scancode == 0xB6) { shift_pressed = 0; extended_mode = 0; goto end; }

    // Zustand für normale Tasten zurücksetzen
    extended_mode = 0;
    if (scancode & 0x80) goto end; // Break-Codes normaler Tasten ignorieren

    char character = 0;
    if (scancode < 128) {
        if (altgr_pressed) {
            character = keymap_altgr[scancode];
        } else if (shift_pressed) {
            character = keymap_shift[scancode];
        } else {
            character = keymap_normal[scancode];
        }
    }

    if (character != 0) {
        if (character == '\n') {
            execute_command(); 
        }
        else if (character == '\b') {
            if (command_length > 0) {
                command_length--;
                cursor_position -= 2;
                video_memory[cursor_position] = ' ';
                video_memory[cursor_position + 1] = 0x07;
            }
        }
        else {
            if (command_length < 63) {
                command_buffer[command_length++] = character;
                video_memory[cursor_position] = character;
                video_memory[cursor_position + 1] = 0x0F;
                cursor_position += 2;
            }
        }
    }

end:
    // Sicherer EOI für Master und ggf. Slave PIC, damit echte Hardware nicht blockiert
    outb(0x20, 0x20); 
}
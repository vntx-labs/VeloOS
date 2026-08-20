// keyboard.c - Dummer Tastatur-Treiber (liefert nur rohe Zeichen an den Kernel)
#include "keyboard.h"
#include <efi.h>
#include <efilib.h>

// Hardware Port I/O
static inline unsigned char inb(unsigned short port) {
    unsigned char result;
    __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

// Tastatur-Zustand (intern für Shift)
static int shift_active = 0;

// Deutsche QWERTZ Keymap
static const char keymap_normal[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5', 
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0', 
    [0x0C] = '-', [0x0D] = '=', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't', 
    [0x15] = 'z', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p', 
    [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g', 
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x2C] = 'y', 
    [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n', 
    [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '-', [0x39] = ' '
};

static const char keymap_shift[128] = {
    [0x02] = '!', [0x03] = '"', [0x05] = '$', [0x06] = '%', 
    [0x07] = '&', [0x08] = '/', [0x09] = '(', [0x0A] = ')', [0x0B] = '=', 
    [0x0C] = '?', [0x0D] = '`', [0x10] = 'Q', [0x11] = 'W', 
    [0x12] = 'E', [0x13] = 'R', [0x14] = 'T', [0x15] = 'Z', [0x16] = 'U', 
    [0x17] = 'I', [0x18] = 'O', [0x19] = 'P', [0x1E] = 'A', [0x1F] = 'S', 
    [0x20] = 'D', [0x21] = 'F', [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', 
    [0x25] = 'K', [0x26] = 'L', [0x2C] = 'Y', [0x2D] = 'X', [0x2E] = 'C', 
    [0x2F] = 'V', [0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = ';', 
    [0x34] = ':', [0x35] = '_', [0x39] = ' '
};

void init_keyboard() {
    while (inb(0x64) & 0x01) {
        inb(0x60);
    }
}

// Der Treiber ist jetzt "dumm": Er holt die Taste ab, übersetzt sie via Keymap 
// und gibt das ASCII-Zeichen (oder Steuerungssichen wie \b, \n, \t) an den Kernel zurück.
char poll_keyboard_ascii() {
    if (!(inb(0x64) & 0x01)) {
        return 0; // Keine Taste gedrückt
    }

    unsigned char scancode = inb(0x60);

    // Shift Press (Left/Right Shift)
    if (scancode == 0x2A || scancode == 0x36) {
        shift_active = 1;
        return 0;
    }
    // Shift Release
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_active = 0;
        return 0;
    }

    // Key Release ignorieren (außer Shift, was oben abgefangen wird)
    if (scancode & 0x80) {
        return 0;
    }

    // Sonderbehandlung für Tasten wie Tab direkt über Scancode, falls nötig
    if (scancode == 0x0F) return '\t';

    // In ASCII übersetzen
    if (scancode < 128) {
        return shift_active ? keymap_shift[scancode] : keymap_normal[scancode];
    }

    return 0;
}
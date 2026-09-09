#include "keyboard.h"
#include <efi.h>
#include <efilib.h>

#define KBC_DATA        0x60
#define KBC_STATUS      0x64
#define KBC_STAT_OBF    0x01
#define KBC_STAT_AUX    0x20

static int g_current_layout = LAYOUT_QWERTZ;
static int shift_active = 0;
static int ctrl_active = 0;
static int alt_active = 0;
static int altgr_active = 0;
static int delete_active = 0;
static int e0_prefix = 0;
static int g_caps_lock = 0;
static int g_num_lock = 1;

int keyboard_is_ctrl(void)   { return ctrl_active; }
int keyboard_is_alt(void)    { return alt_active; }
int keyboard_is_shift(void)  { return shift_active; }
int keyboard_is_delete(void) { return delete_active; }

static inline unsigned char inb_kbc_key(unsigned short port) {
    unsigned char result;
    __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

void keyboard_set_layout(int layout) {
    g_current_layout = layout;
}

int keyboard_get_layout(void) {
    return g_current_layout;
}

char keyboard_translate_char(char c) {
    return c;
}

static const char keymap_qwertz_normal[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = (char)0xDF, [0x0D] = '`',  [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'z', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = (char)0xFC, [0x1B] = '+', [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x27] = (char)0xF6, [0x28] = (char)0xE4, [0x29] = '^', [0x2B] = '#',
    [0x2C] = 'y', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '-',
    [0x39] = ' ', [0x56] = '<'
};

static const char keymap_qwertz_shift[128] = {
    [0x02] = '!', [0x03] = '"', [0x04] = (char)0xA7, [0x05] = '$', [0x06] = '%',
    [0x07] = '&', [0x08] = '/', [0x09] = '(', [0x0A] = ')', [0x0B] = '=',
    [0x0C] = '?', [0x0D] = '`', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
    [0x15] = 'Z', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
    [0x1A] = (char)0xDC, [0x1B] = '*', [0x1C] = '\n',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
    [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L',
    [0x27] = (char)0xD6, [0x28] = (char)0xC4, [0x29] = (char)0xB0, [0x2B] = '\'',
    [0x2C] = 'Y', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B',
    [0x31] = 'N', [0x32] = 'M', [0x33] = ';', [0x34] = ':', [0x35] = '_',
    [0x39] = ' ', [0x56] = '>'
};

static const char keymap_qwertz_altgr[128] = {
    [0x03] = (char)0xB2, [0x04] = (char)0xB3,
    [0x08] = '{', [0x09] = '[', [0x0A] = ']', [0x0B] = '}', [0x0C] = '\\',
    [0x10] = '@', [0x12] = (char)0x80, [0x1B] = '~', [0x32] = (char)0xB5, [0x56] = '|'
};

static const char keymap_qwerty_normal[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '-', [0x0D] = '=', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']', [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x27] = ';', [0x28] = '\'', [0x29] = '`', [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/', 
    [0x39] = ' '
};

static const char keymap_qwerty_shift[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%',
    [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
    [0x0C] = '_', [0x0D] = '+', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
    [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
    [0x1A] = '{', [0x1B] = '}', [0x1C] = '\n',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
    [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L',
    [0x27] = ':', [0x28] = '"', [0x29] = '~', [0x2B] = '|',
    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B',
    [0x31] = 'N', [0x32] = 'M', [0x33] = '<', [0x34] = '>', [0x35] = '?', 
    [0x39] = ' '
};

void init_keyboard(void) {
    shift_active = 0;
    ctrl_active = 0;
    alt_active = 0;
    altgr_active = 0;
    delete_active = 0;
    e0_prefix = 0;
    g_caps_lock = 0;
    g_num_lock = 1;
}

void keyboard_drain(void) {
    int max = 32;
    while ((inb_kbc_key(KBC_STATUS) & KBC_STAT_OBF) && --max) {
        inb_kbc_key(KBC_DATA);
    }
}

char poll_keyboard_ascii(void) {
    unsigned char stat = inb_kbc_key(KBC_STATUS);
    if (!(stat & KBC_STAT_OBF) || (stat & KBC_STAT_AUX)) return 0;

    unsigned char scancode = inb_kbc_key(KBC_DATA);
    if (scancode == 0xE0) { e0_prefix = 1; return 0; }
    if (scancode == 0xFA || scancode == 0xFE) return 0;

    // Strg Taste
    if (scancode == 0x1D) { ctrl_active = 1; e0_prefix = 0; return 0; }
    if (scancode == 0x9D) { ctrl_active = 0; e0_prefix = 0; return 0; }

    // Alt Taste (Links)
    if (!e0_prefix && scancode == 0x38) { alt_active = 1; return 0; }
    if (!e0_prefix && scancode == 0xB8) { alt_active = 0; return 0; }

    // AltGr Taste (Rechts)
    if (e0_prefix && scancode == 0x38) { altgr_active = 1; e0_prefix = 0; return 0; }
    if (e0_prefix && scancode == 0xB8) { altgr_active = 0; e0_prefix = 0; return 0; }
    
    // Shift Tasten
    if (scancode == 0x2A || scancode == 0x36) { shift_active = 1; e0_prefix = 0; return 0; }
    if (scancode == 0xAA || scancode == 0xB6) { shift_active = 0; e0_prefix = 0; return 0; }
    
    // Entf Release Tracking
    if (e0_prefix && scancode == 0xD3) { delete_active = 0; e0_prefix = 0; return 0; }
    if (!e0_prefix && scancode == 0xD3) { delete_active = 0; return 0; }

    // Lock Tasten
    if (scancode == 0x3A) { g_caps_lock = !g_caps_lock; e0_prefix = 0; return 0; }
    if (scancode == 0x45 && !e0_prefix) { g_num_lock = !g_num_lock; return 0; }

    // Break/Release Codes
    if (scancode & 0x80) { e0_prefix = 0; return 0; }

    // E0-Erweiterte Navigation
    if (e0_prefix) {
        e0_prefix = 0;
        if (scancode == 0x5B || scancode == 0x5C) return KEY_SUPER;
        if (scancode == 0x48) return KEY_UP;
        if (scancode == 0x50) return KEY_DOWN;
        if (scancode == 0x4B) return KEY_LEFT;
        if (scancode == 0x4D) return KEY_RIGHT;
        if (scancode == 0x47) return KEY_HOME;
        if (scancode == 0x4F) return KEY_END;
        if (scancode == 0x53) { delete_active = 1; return KEY_DELETE; }
        if (scancode == 0x35) return '/';
        if (scancode == 0x1C) return '\n';
        return 0;
    }

    // Ziffernblock
    if (scancode == 0x47) return g_num_lock ? '7' : KEY_HOME;
    if (scancode == 0x48) return g_num_lock ? '8' : KEY_UP;
    if (scancode == 0x49) return g_num_lock ? '9' : 0;
    if (scancode == 0x4A) return '-';
    if (scancode == 0x4B) return g_num_lock ? '4' : KEY_LEFT;
    if (scancode == 0x4C) return g_num_lock ? '5' : 0;
    if (scancode == 0x4D) return g_num_lock ? '6' : KEY_RIGHT;
    if (scancode == 0x4E) return '+';
    if (scancode == 0x4F) return g_num_lock ? '1' : KEY_END;
    if (scancode == 0x50) return g_num_lock ? '2' : KEY_DOWN;
    if (scancode == 0x51) return g_num_lock ? '3' : 0;
    if (scancode == 0x52) return g_num_lock ? '0' : 0;
    if (scancode == 0x53) {
        if (g_num_lock) return (g_current_layout == LAYOUT_QWERTZ ? ',' : '.');
        delete_active = 1;
        return KEY_DELETE;
    }

    if (scancode == 0x01) return KEY_ESC;
    if (scancode == 0x3B) return KEY_F1;
    if (scancode == 0x1C) return '\n';
    if (scancode == 0x0E) return '\b';
    if (scancode == 0x0F) return '\t';

    if (scancode < 128) {
        char c = 0;
        if (g_current_layout == LAYOUT_QWERTZ) {
            if (altgr_active) c = keymap_qwertz_altgr[scancode];
            else if (shift_active) c = keymap_qwertz_shift[scancode];
            else c = keymap_qwertz_normal[scancode];
        } else {
            if (shift_active) c = keymap_qwerty_shift[scancode];
            else c = keymap_qwerty_normal[scancode];
        }

        if (ctrl_active && !alt_active) {
            if (c == 'c' || c == 'C') return KEY_CTRL_C;
            if (c == 'v' || c == 'V') return KEY_CTRL_V;
            if (c == 'x' || c == 'X') return KEY_CTRL_X;
            if (c == 'a' || c == 'A') return KEY_CTRL_A;
            if (c == 'u' || c == 'U') return (char)0x15;
            if (c == 'd' || c == 'D') return (char)0x04;
        }

        if (g_caps_lock && c != 0 && !altgr_active) {
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            else if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        }
        return c;
    }

    return 0;
}
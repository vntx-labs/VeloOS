#include "keyboard.h"
#include <efi.h>
#include <efilib.h>

#ifndef EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID
static EFI_GUID g_text_input_ex_guid = {
    0xdd9e3968, 0x4469, 0x4250,
    {0xa0, 0x46, 0x4a, 0x22, 0xcb, 0x76, 0xca, 0x76}
};
#else
static EFI_GUID g_text_input_ex_guid = EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;
#endif

#define MAX_KEYBOARD_HANDLES 8
static EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *g_keyboards[MAX_KEYBOARD_HANDLES];
static UINTN g_keyboard_count = 0;

static int g_current_layout = LAYOUT_QWERTZ;
static int shift_active = 0;
static int ctrl_active = 0;
static int alt_active = 0;
static int altgr_active = 0;
static int delete_active = 0;
static int e0_prefix = 0;

int keyboard_is_ctrl(void)   { return ctrl_active; }
int keyboard_is_alt(void)    { return alt_active; }
int keyboard_is_shift(void)  { return shift_active; }
int keyboard_is_delete(void) { return delete_active; }

static inline unsigned char inb_kbc_key(unsigned short port) {
    unsigned char result;
    __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outb_kbc_key(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
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

/* =========================================================================
 * PS/2 SCANCODE TABELLEN (PORT 0x60 / SET 1)
 * ========================================================================= */

/* 1. QWERTZ Normal */
static const char keymap_qwertz[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = (char)0xDF, /* ß */
    [0x0D] = '`',
    [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'z', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = (char)0xFC, /* ü */
    [0x1B] = '+', [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x27] = (char)0xF6, /* ö */
    [0x28] = (char)0xE4, /* ä */
    [0x29] = '^', 
    [0x2B] = '#',        /* Taste rechts neben Ä */
    [0x2C] = 'y', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm', 
    [0x33] = ',', 
    [0x34] = '.', 
    [0x35] = '-',
    [0x39] = ' ', 
    [0x56] = '<'         /* Die Taste zwischen Shift-Links und Y */
};

/* 2. QWERTZ Shift */
static const char keymap_qwertz_shift[128] = {
    [0x02] = '!', [0x03] = '"', [0x04] = (char)0xA7, /* § */
    [0x05] = '$', [0x06] = '%', [0x07] = '&', [0x08] = '/', [0x09] = '(', 
    [0x0A] = ')', [0x0B] = '=',
    [0x0C] = '?', [0x0D] = '`',  [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
    [0x15] = 'Z', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
    [0x1A] = (char)0xDC, /* Ü */
    [0x1B] = '*', [0x1C] = '\n',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
    [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L',
    [0x27] = (char)0xD6, /* Ö */
    [0x28] = (char)0xC4, /* Ä */
    [0x29] = (char)0xB0, /* ° */
    [0x2B] = '\'',       /* Shift + # = ' */
    [0x2C] = 'Y', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B',
    [0x31] = 'N', [0x32] = 'M', 
    [0x33] = ';',        /* Shift + , = ; */
    [0x34] = ':',        /* Shift + . = : */
    [0x35] = '_',        /* Shift + - = _ */
    [0x39] = ' ', 
    [0x56] = '>'         /* Shift + Taste zwischen Shift-Links und Y */
};

/* 3. QWERTZ AltGr */
static const char keymap_qwertz_altgr[128] = {
    [0x03] = (char)0xB2, /* ² */
    [0x04] = (char)0xB3, /* ³ */
    [0x08] = '{',
    [0x09] = '[',
    [0x0A] = ']',
    [0x0B] = '}',
    [0x0C] = '\\',
    [0x10] = '@',
    [0x12] = 'E',
    [0x1B] = '~',
    [0x56] = '|'         /* AltGr + Taste zwischen Shift-Links und Y */
};

/* 4. QWERTY Normal */
static const char keymap_qwerty[128] = {
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
    [0x39] = ' ', [0x56] = '<'
};

/* 5. QWERTY Shift */
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
    [0x39] = ' ', [0x56] = '>'
};

/* =========================================================================
 * UEFI UNICODE KONVERTIERUNG (FALLBACK)
 * ========================================================================= */
static char convert_uefi_unicode_to_qwertz(CHAR16 u, int is_shift, int is_altgr) {
    if (is_altgr || (ctrl_active && alt_active)) {
        if (u == L'q' || u == L'Q' || u == L'@') return '@';
        if (u == L'7' || u == L'&' || u == L'/') return '{';
        if (u == L'8' || u == L'*' || u == L'(') return '[';
        if (u == L'9' || u == L'(' || u == L')') return ']';
        if (u == L'0' || u == L')' || u == L'=') return '}';
        if (u == L'e' || u == L'E') return 'E';
        if (u == L'-' || u == L'_' || (unsigned char)u == 0xDF) return '\\';
        if (u == L'+' || u == L'*' || u == L']' || u == L'}') return '~';
        if (u == L'<' || u == L'>' || u == L'|') return '|';
    }

    if (u == L'y') return 'z';
    if (u == L'z') return 'y';
    if (u == L'Y') return 'Z';
    if (u == L'Z') return 'Y';

    /* Shift + Komma = ; | Shift + Punkt = : */
    if (u == L'<') return is_shift ? ';' : '<';
    if (u == L'>') return is_shift ? ':' : '>';
    if (u == L',' && is_shift) return ';';
    if (u == L'.' && is_shift) return ':';
    if (u == L',' && !is_shift) return ',';
    if (u == L'.' && !is_shift) return '.';

    /* Shift + Zahlen */
    if (u == L'!') return '!';
    if (u == L'@') return '"';            /* Shift + 2 = " */
    if (u == L'#') return is_shift ? (char)0xA7 : '#';
    if (u == L'$') return '$';            /* Shift + 4 = $ */
    if (u == L'%') return '%';            /* Shift + 5 = % */
    if (u == L'^') return '&';            /* Shift + 6 = & */
    if (u == L'&') return '/';            /* Shift + 7 = / */
    if (u == L'*') return '(';            /* Shift + 8 = ( */
    if (u == L'(') return ')';            /* Shift + 9 = ) */
    if (u == L')') return '=';            /* Shift + 0 = = */

    if (is_shift) {
        if (u == L'1') return '!';
        if (u == L'2') return '"';
        if (u == L'3') return (char)0xA7; /* § */
        if (u == L'4') return '$';
        if (u == L'5') return '%';
        if (u == L'6') return '&';
        if (u == L'7') return '/';
        if (u == L'8') return '(';
        if (u == L'9') return ')';
        if (u == L'0') return '=';
    }

    /* Umlaute und Sonderzeichen */
    if (u == L'[') return (char)0xFC;     /* ü */
    if (u == L'{') return (char)0xDC;     /* Ü */
    if (u == L';') return (char)0xF6;     /* ö */
    if (u == L':') return (char)0xD6;     /* Ö */
    if (u == L'\'') return (char)0xE4;    /* ä */
    if (u == L'"') return (char)0xC4;     /* Ä */
    if (u == L'-') return (char)0xDF;     /* ß */
    if (u == L'_') return '?';            /* Shift + ß = ? */
    if (u == L'=') return '`';
    if (u == L'+') return '*';
    if (u == L']') return '+';
    if (u == L'}') return '*';
    if (u == L'\\') return is_shift ? '\'' : '#';
    if (u == L'|') return is_shift ? '\'' : '#';
    if (u == L'/') return '-';
    if (u == L'?') return '_';
    if (u == L'`') return '^';
    if (u == L'~') return (char)0xB0;     /* Shift + ^ = ° */

    return (char)(u & 0xFF);
}

void init_keyboard(void) {
    shift_active = 0;
    ctrl_active = 0;
    alt_active = 0;
    altgr_active = 0;
    delete_active = 0;
    e0_prefix = 0;
    g_keyboard_count = 0;

    outb_kbc_key(0x64, 0xAE);

    if (ST && ST->ConIn) {
        uefi_call_wrapper(ST->ConIn->Reset, 2, ST->ConIn, FALSE);
    }

    if (!BS) return;

    UINTN handle_count = 0;
    EFI_HANDLE *handles = NULL;
    EFI_STATUS status = uefi_call_wrapper(BS->LocateHandleBuffer, 5,
                                          ByProtocol, &g_text_input_ex_guid,
                                          NULL, &handle_count, &handles);

    if (!EFI_ERROR(status) && handles) {
        for (UINTN i = 0; i < handle_count && g_keyboard_count < MAX_KEYBOARD_HANDLES; i++) {
            EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *kbd = NULL;
            status = uefi_call_wrapper(BS->HandleProtocol, 3, handles[i], &g_text_input_ex_guid, (void**)&kbd);
            if (!EFI_ERROR(status) && kbd) {
                uefi_call_wrapper(kbd->Reset, 2, kbd, FALSE);
                g_keyboards[g_keyboard_count++] = kbd;
            }
        }
        uefi_call_wrapper(BS->FreePool, 1, handles);
    }
}

void keyboard_drain(void) {
    for (UINTN i = 0; i < g_keyboard_count; i++) {
        EFI_KEY_DATA kdata;
        while (!EFI_ERROR(uefi_call_wrapper(g_keyboards[i]->ReadKeyStrokeEx, 2, g_keyboards[i], &kdata))) {}
    }
    if (ST && ST->ConIn) {
        EFI_INPUT_KEY ikey;
        while (!EFI_ERROR(uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &ikey))) {}
    }
    int max = 16;
    while ((inb_kbc_key(0x64) & 0x01) && --max) {
        inb_kbc_key(0x60);
    }
}

char poll_keyboard_ascii(void) {
    delete_active = 0;

    // =========================================================================
    // 1. HARDWARE PORT 0x60 (Höchste Priorität für exakte Scancodes 0x56 & 0x2B)
    // =========================================================================
    unsigned char stat = inb_kbc_key(0x64);
    if ((stat & 0x01) && !(stat & 0x20)) {
        unsigned char scancode = inb_kbc_key(0x60);
        
        // E0 Prefix Behandlung
        if (scancode == 0xE0) { 
            e0_prefix = 1; 
            return 0; 
        }

        // Strg
        if (scancode == 0x1D) { ctrl_active = 1; e0_prefix = 0; return 0; }
        if (scancode == 0x9D) { ctrl_active = 0; e0_prefix = 0; return 0; }
        
        // AltGr (E0 + 38)
        if (e0_prefix && scancode == 0x38) { altgr_active = 1; e0_prefix = 0; return 0; }
        if (e0_prefix && scancode == 0xB8) { altgr_active = 0; e0_prefix = 0; return 0; }

        // Normales Alt (38)
        if (!e0_prefix && scancode == 0x38) { alt_active = 1; return 0; }
        if (!e0_prefix && scancode == 0xB8) { alt_active = 0; return 0; }

        // Shift (Links 2A, Rechts 36)
        if (scancode == 0x2A || scancode == 0x36) { shift_active = 1; e0_prefix = 0; return 0; }
        if (scancode == 0xAA || scancode == 0xB6) { shift_active = 0; e0_prefix = 0; return 0; }

        // Key-Release Events ignorieren
        if (scancode & 0x80) { e0_prefix = 0; return 0; }

        // Extended Tasten mit E0 Prefix
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
            if (scancode == 0x1C) return '\n';
            return 0;
        }

        // Standard Navigation / Sonder-Tasten
        if (scancode == 0x01) return KEY_ESC;
        if (scancode == 0x3B) return KEY_F1;
        if (scancode == 0x1C) return '\n';
        if (scancode == 0x0E) return '\b';
        if (scancode == 0x0F) return '\t';

        // Zeichen-Decoding (Set 1)
        if (scancode < 128) {
            char c = 0;
            int is_ag = altgr_active || (ctrl_active && alt_active);

            // 1. Die ISO-Taste links neben Y (Scancode 0x56)
            if (scancode == 0x56) {
                if (is_ag) return '|';
                if (shift_active) return '>';
                return '<';
            }

            // 2. Die Hash-Taste rechts neben Ä (Scancode 0x2B)
            if (scancode == 0x2B) {
                if (shift_active) return '\'';
                return '#';
            }

            if (g_current_layout == LAYOUT_QWERTZ) {
                if (is_ag && keymap_qwertz_altgr[scancode]) {
                    c = keymap_qwertz_altgr[scancode];
                } else if (shift_active) {
                    c = keymap_qwertz_shift[scancode];
                } else {
                    c = keymap_qwertz[scancode];
                }
            } else {
                c = shift_active ? keymap_qwerty_shift[scancode] : keymap_qwerty[scancode];
            }

            if (ctrl_active && !alt_active && c != 0) {
                if (c == 'c' || c == 'C') return KEY_CTRL_C;
                if (c == 'v' || c == 'V') return KEY_CTRL_V;
                if (c == 'x' || c == 'X') return KEY_CTRL_X;
                if (c == 'a' || c == 'A') return KEY_CTRL_A;
                if (c == 'z' || c == 'Z') return KEY_CTRL_Z;
                if (c == 'y' || c == 'Y') return KEY_CTRL_Y;
            }
            return c;
        }
    }

    // =========================================================================
    // 2. UEFI USB Keyboard Extended Protocol (Fallback)
    // =========================================================================
    for (UINTN i = 0; i < g_keyboard_count; i++) {
        EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *kbd = g_keyboards[i];
        if (!kbd) continue;

        EFI_KEY_DATA kd;
        EFI_STATUS st = uefi_call_wrapper(kbd->ReadKeyStrokeEx, 2, kbd, &kd);
        if (!EFI_ERROR(st)) {
            if (kd.KeyState.KeyShiftState & EFI_SHIFT_STATE_VALID) {
                shift_active = (kd.KeyState.KeyShiftState & (EFI_LEFT_SHIFT_PRESSED | EFI_RIGHT_SHIFT_PRESSED)) ? 1 : 0;
                ctrl_active  = (kd.KeyState.KeyShiftState & (EFI_LEFT_CONTROL_PRESSED | EFI_RIGHT_CONTROL_PRESSED)) ? 1 : 0;
                alt_active   = (kd.KeyState.KeyShiftState & (EFI_LEFT_ALT_PRESSED | EFI_RIGHT_ALT_PRESSED)) ? 1 : 0;
                altgr_active = (kd.KeyState.KeyShiftState & EFI_RIGHT_ALT_PRESSED) ? 1 : 0;
                if (kd.KeyState.KeyShiftState & (EFI_LEFT_LOGO_PRESSED | EFI_RIGHT_LOGO_PRESSED)) return KEY_SUPER;
            }

            switch (kd.Key.ScanCode) {
                case 0x0001: return KEY_UP;
                case 0x0002: return KEY_DOWN;
                case 0x0003: return KEY_RIGHT;
                case 0x0004: return KEY_LEFT;
                case 0x0005: return KEY_HOME;
                case 0x0006: return KEY_END;
                case 0x0008: delete_active = 1; return KEY_DELETE;
                case 0x0017: return KEY_ESC;
                case 0x000B: return KEY_F1;
                default: break;
            }

            CHAR16 u = kd.Key.UnicodeChar;
            if (u == L'\r') return '\n';
            if (u == L'\b') return '\b';
            if (u == L'\t') return '\t';

            if (ctrl_active && !alt_active) {
                if (u == L'a' || u == L'A') return KEY_CTRL_A;
                if (u == L'b' || u == L'B') return KEY_CTRL_B;
                if (u == L'c' || u == L'C') return KEY_CTRL_C;
                if (u == L'f' || u == L'F') return KEY_CTRL_F;
                if (u == L'n' || u == L'N') return KEY_CTRL_N;
                if (u == L'o' || u == L'O') return KEY_CTRL_O;
                if (u == L's' || u == L'S') return KEY_CTRL_S;
                if (u == L'v' || u == L'V') return KEY_CTRL_V;
                if (u == L'x' || u == L'X') return KEY_CTRL_X;
                if (u == L'y' || u == L'Y') return KEY_CTRL_Y;
                if (u == L'z' || u == L'Z') return KEY_CTRL_Z;
            }

            if (g_current_layout == LAYOUT_QWERTZ) {
                char res = convert_uefi_unicode_to_qwertz(u, shift_active, altgr_active);
                if (res != 0) return res;
            } else if (u >= 32) {
                return (char)(u & 0xFF);
            }
        }
    }

    // =========================================================================
    // 3. Standard UEFI ConIn Protocol (Fallback)
    // =========================================================================
    if (ST && ST->ConIn) {
        EFI_INPUT_KEY ikey;
        EFI_STATUS st = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &ikey);
        if (!EFI_ERROR(st)) {
            switch (ikey.ScanCode) {
                case 0x0001: return KEY_UP;
                case 0x0002: return KEY_DOWN;
                case 0x0003: return KEY_RIGHT;
                case 0x0004: return KEY_LEFT;
                case 0x0005: return KEY_HOME;
                case 0x0006: return KEY_END;
                case 0x0008: delete_active = 1; return KEY_DELETE;
                case 0x0017: return KEY_ESC;
                case 0x000B: return KEY_F1;
                default: break;
            }

            CHAR16 u = ikey.UnicodeChar;
            if (u == L'\r') return '\n';
            if (u == L'\b') return '\b';
            if (u == L'\t') return '\t';

            if (ctrl_active && !alt_active) {
                if (u == L'a' || u == L'A') return KEY_CTRL_A;
                if (u == L'b' || u == L'B') return KEY_CTRL_B;
                if (u == L'c' || u == L'C') return KEY_CTRL_C;
                if (u == L'f' || u == L'F') return KEY_CTRL_F;
                if (u == L'n' || u == L'N') return KEY_CTRL_N;
                if (u == L'o' || u == L'O') return KEY_CTRL_O;
                if (u == L's' || u == L'S') return KEY_CTRL_S;
                if (u == L'v' || u == L'V') return KEY_CTRL_V;
                if (u == L'x' || u == L'X') return KEY_CTRL_X;
                if (u == L'y' || u == L'Y') return KEY_CTRL_Y;
                if (u == L'z' || u == L'Z') return KEY_CTRL_Z;
            }

            if (g_current_layout == LAYOUT_QWERTZ) {
                char res = convert_uefi_unicode_to_qwertz(u, shift_active, altgr_active);
                if (res != 0) return res;
            } else if (u >= 32) {
                return (char)(u & 0xFF);
            }
        }
    }

    return 0;
}
#ifndef KEYBOARD_H
#define KEYBOARD_H

#define LAYOUT_QWERTZ 0
#define LAYOUT_QWERTY 1

#define KEY_SUPER     ((char)0x80)
#define KEY_F1        ((char)0x81)
#define KEY_ESC       ((char)0x1B)

// Navigations- & Pfeiltasten
#define KEY_UP        ((char)0x82)
#define KEY_DOWN      ((char)0x83)
#define KEY_LEFT      ((char)0x84)
#define KEY_RIGHT     ((char)0x85)
#define KEY_HOME      ((char)0x86)
#define KEY_END       ((char)0x87)
#define KEY_DELETE    ((char)0x88)

// Globale Standard-Shortcuts (ASCII 1 bis 26 für Ctrl+A .. Ctrl+Z)
#define KEY_CTRL_A    ((char)0x01)
#define KEY_CTRL_B    ((char)0x02)
#define KEY_CTRL_C    ((char)0x03)
#define KEY_CTRL_F    ((char)0x06)
#define KEY_CTRL_N    ((char)0x0E)
#define KEY_CTRL_O    ((char)0x0F)
#define KEY_CTRL_S    ((char)0x13)
#define KEY_CTRL_V    ((char)0x16)
#define KEY_CTRL_X    ((char)0x18)
#define KEY_CTRL_Y    ((char)0x19)
#define KEY_CTRL_Z    ((char)0x1A)

void init_keyboard(void);
void keyboard_drain(void);
char poll_keyboard_ascii(void);
void keyboard_set_layout(int layout);
int  keyboard_get_layout(void);
char keyboard_translate_char(char c);

/* Modifier-Abfragen fuer Shortcuts (Strg, Alt, Shift, Delete) */
int keyboard_is_ctrl(void);
int keyboard_is_alt(void);
int keyboard_is_shift(void);
int keyboard_is_delete(void);

#endif /* KEYBOARD_H */
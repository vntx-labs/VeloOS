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

void init_keyboard(void);
void init_keyboard_bare_metal(void);
void keyboard_drain(void);
char poll_keyboard_ascii(void);
void keyboard_set_layout(int layout);
int  keyboard_get_layout(void);
char keyboard_translate_char(char c);

#endif
#ifndef KEYBOARD_H
#define KEYBOARD_H

void init_keyboard(void);
void init_keyboard_bare_metal(void);
void keyboard_drain(void);
char poll_keyboard_ascii(void);

#endif

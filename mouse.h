#ifndef MOUSE_H
#define MOUSE_H

#include <efi.h>
#include <efilib.h>

#define CURSOR_ARROW 0
#define CURSOR_HAND  1
#define CURSOR_IBEAM 2

typedef struct {
    int x;
    int y;
    int left_button;
    int right_button;
    int left_clicked;
    int right_clicked;
    int cursor_type;
} MouseState;

void mouse_init(void);
int mouse_update(void);
MouseState* mouse_get_state(void);
void mouse_set_cursor(int cursor_type);
void mouse_draw_cursor(void);

#endif
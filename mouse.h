#ifndef MOUSE_H
#define MOUSE_H

#include <efi.h>
#include <efilib.h>

typedef struct {
    int x;
    int y;
    int left_button;
    int right_button;
    int left_clicked;
    int right_clicked;
} MouseState;

void mouse_init(void);
int mouse_update(void);
MouseState* mouse_get_state(void);
void mouse_draw_cursor(void);

#endif
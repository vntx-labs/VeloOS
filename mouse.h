#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

#ifndef UINT32
typedef uint8_t   UINT8;
typedef uint16_t  UINT16;
typedef uint32_t  UINT32;
typedef uint64_t  UINT64;
typedef int64_t   INT64;
typedef uint64_t  UINTN;
typedef void      VOID;
#endif

#define CURSOR_ARROW 0
#define CURSOR_HAND  1
#define CURSOR_IBEAM 2

typedef struct {
    int x;
    int y;
    int left_button;
    int right_button;
    int middle_button;
    int left_clicked;
    int right_clicked;
    int left_released;
    int scroll_z;   // Vertikales Rad (+1 / -1)
    int scroll_h;   // Horizontales Rad (+1 / -1)
    int cursor_type;
} MouseState;

void mouse_init(void);
int  mouse_update(void);
MouseState* mouse_get_state(void);
void mouse_set_cursor(int cursor_type);
void mouse_draw_cursor(void);

#endif /* MOUSE_H */
#ifndef WM_H
#define WM_H

#include <efi.h>
#include <efilib.h>

#define MAX_WINDOWS 8

typedef struct {
    int id;
    int x;
    int y;
    int width;
    int height;
    char title[64];
    int is_active;
    int is_minimized;
    int is_closed;
    UINT32 bg_color;
    void (*on_paint)(int win_id, int content_x, int content_y, int content_w, int content_h);
    void (*on_click)(int win_id, int local_x, int local_y);
    void (*on_key)(int win_id, char key);
} Window;

/* Dirty Rectangle Engine */
void wm_mark_dirty(int x, int y, int w, int h);
void wm_mark_all_dirty(void);
int wm_is_dirty(void);
void wm_get_dirty_bounds(int *dx, int *dy, int *dw, int *dh);
void wm_clear_dirty(void);

/* Window Manager Core */
void wm_init(void);
int wm_create_window(int x, int y, int w, int h, const char *title, void (*on_paint)(int, int, int, int, int));
int wm_create_window_auto(const char *title, int content_w, int content_h, void (*on_paint)(int, int, int, int, int));
int wm_create_window_for_lines(const char *title, const char **lines, int line_count, int scale, void (*on_paint)(int, int, int, int, int));

void wm_close_window(int id);
void wm_close_active(void);
void wm_minimize_window(int id);
void wm_minimize_active(void);
void wm_focus_window(int id);
void wm_focus_next(void);
Window* wm_get_window(int id);
int wm_get_window_count(void);
int wm_get_active_window_id(void);
void wm_render_all(void);

/* Drag & Drop Fensterverschiebung */
void wm_start_drag(int win_id, int mouse_x, int mouse_y);
void wm_update_drag(int mouse_x, int mouse_y);
void wm_stop_drag(void);
int wm_is_dragging(void);

/* Typografie & Text */
void wm_draw_text(const char *str, int x, int y, UINT32 fg_color, UINT32 bg_color);
void wm_draw_string_content(const char *str, int x, int y, int scale, UINT32 fg_color, UINT32 bg_color, int cx, int cy, int cw, int ch);

/* Anti-Aliasing Primitives */
UINT32 alpha_blend(UINT32 fg, UINT32 bg, UINT32 alpha);
void draw_line_aa(int x1, int y1, int x2, int y2, int thickness, UINT32 color);
void draw_circle_aa(int cx, int cy, int radius, int thickness, UINT32 color);
void draw_rounded_rect_aa(int sx, int sy, int w, int h, int r, UINT32 color);
void draw_rounded_rect_gradient(int sx, int sy, int w, int h, int r, UINT32 top_col, UINT32 bot_col);

#endif
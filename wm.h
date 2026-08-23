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
} Window;

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

/* Automatische Textvermessung & Hardware-Clipping */
void wm_measure_string(const char *str, int scale, int *out_w, int *out_h);
void wm_measure_text_lines(const char **lines, int line_count, int scale, int *out_w, int *out_h);
void wm_draw_string_content(const char *str, int x, int y, int scale, UINT32 fg_color, UINT32 bg_color, int cx, int cy, int cw, int ch);

/* Analytische Vektor- & Anti-Aliasing-Primitives mit Subpixel-Blending */
UINT32 alpha_blend(UINT32 fg, UINT32 bg, UINT32 alpha);
void draw_line_aa(int x1, int y1, int x2, int y2, int thickness, UINT32 color);
void draw_circle_aa(int cx, int cy, int radius, int thickness, UINT32 color);
void draw_filled_circle_aa(int cx, int cy, int radius, UINT32 color);
void draw_rounded_rect_aa(int sx, int sy, int w, int h, int r, UINT32 color);
void draw_rounded_rect_gradient(int sx, int sy, int w, int h, int r, UINT32 top_col, UINT32 bot_col);

#endif
#ifndef WM_H
#define WM_H

#include <efi.h>
#include <efilib.h>

#define MAX_WINDOWS 64
#define SURFACE_STRIDE 1024
#define SURFACE_HEIGHT 768

typedef struct {
    int id;
    int x;
    int y;
    int width;
    int height;
    int orig_x;
    int orig_y;
    int orig_w;
    int orig_h;
    char title[64];
    int is_active;
    int is_minimized;
    int is_maximized;
    int is_closed;
    int is_dirty_content;
    UINT32 bg_color;
    UINT32 *surface;

    char key_queue[16];
    int key_q_head;
    int key_q_tail;

    void (*on_paint)(int win_id, int content_x, int content_y, int content_w, int content_h);
    void (*on_click)(int win_id, int local_x, int local_y);
    void (*on_key)(int win_id, char key);
} Window;

void wm_mark_dirty(int x, int y, int w, int h);
void wm_mark_all_dirty(void);
int  wm_is_dirty(void);
void wm_get_dirty_bounds(int *dx, int *dy, int *dw, int *dh);
void wm_clear_dirty(void);

void wm_init(void);
int  wm_create_window(int x, int y, int w, int h, const char *title, void (*on_paint)(int, int, int, int, int));
int  wm_create_window_auto(const char *title, int content_w, int content_h, void (*on_paint)(int, int, int, int, int));

void wm_close_window(int id);
void wm_close_active(void);
void wm_minimize_window(int id);
void wm_minimize_active(void);
void wm_minimize_all(void);
void wm_maximize_window(int id);
void wm_maximize_active(void);
void wm_focus_window(int id);
void wm_focus_next(void);
Window* wm_get_window(int id);
int  wm_get_window_count(void);
int  wm_get_active_window_id(void);
void wm_render_all(void);

void wm_window_push_key(int win_id, char key);
char wm_window_pop_key(int win_id);

void wm_start_drag(int win_id, int mouse_x, int mouse_y);
void wm_update_drag(int mouse_x, int mouse_y);
void wm_stop_drag(void);
int  wm_is_dragging(void);

// Surface Rendering für Userland (Kernel-geschützt)
void wm_surface_clear(int win_id, UINT32 color);
void wm_surface_draw_rect(int win_id, int x, int y, int w, int h, UINT32 color);
void wm_surface_draw_gradient(int win_id, int x, int y, int w, int h, UINT32 top_col, UINT32 bot_col);
void wm_surface_draw_text(int win_id, const char *str, int x, int y, UINT32 color);
void wm_surface_draw_icon(int win_id, int icon_type, int x, int y, int size);
void wm_surface_draw_button(int win_id, int x, int y, int w, int h, const char *label);
void wm_surface_draw_storage_bar(int win_id, int x, int y, int w, int percent);
void wm_surface_draw_sidebar_item(int win_id, int y, int w, const char *label, int is_selected);
void wm_surface_draw_nav_btn(int win_id, int x, int y, const char *symbol, int enabled);
void wm_surface_draw_addressbar(int win_id, int x, int y, int w, const char *path);
void wm_surface_draw_searchbox(int win_id, int x, int y, int w, const char *query, int cursor_pos, int focused);
void wm_surface_draw_command_bar(int win_id, int y, int w);
void wm_surface_draw_modal_dialog(int win_id, int x, int y, int w, int h, const char *title);

void wm_draw_text(const char *str, int x, int y, UINT32 fg_color, UINT32 bg_color);
void wm_draw_text_scaled(const char *str, int x, int y, int scale, UINT32 fg_color, UINT32 bg_color);
void wm_draw_text_shadow(const char *str, int x, int y, int scale, UINT32 fg_color, UINT32 shadow_color);
void wm_draw_text_wrapped(const char *str, int x, int y, int max_w, int max_h, UINT32 fg_color, UINT32 bg_color);

UINT32 alpha_blend(UINT32 fg, UINT32 bg, UINT32 alpha);
void draw_line_aa(int x1, int y1, int x2, int y2, int thickness, UINT32 color);
void draw_rounded_rect_aa(int sx, int sy, int w, int h, int r, UINT32 color);
void draw_rounded_rect_gradient(int sx, int sy, int w, int h, int r, UINT32 top_col, UINT32 bot_col);

#endif
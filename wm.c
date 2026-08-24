// wm.c - VeloOS High-Performance Window Manager mit Kanten-Antialiasing & Drag & Drop
#include "wm.h"
#include "font.h"

extern UINTN gop_width;
extern UINTN gop_height;

void put_pixel(UINTN x, UINTN y, UINT32 color);
UINT32 get_pixel(UINTN x, UINTN y);
void draw_filled_rect(UINTN start_x, UINTN start_y, UINTN width, UINTN height, UINT32 color);

static Window g_windows[MAX_WINDOWS];
static int g_window_count = 0;
static int g_active_win_id = -1;

/* Drag & Drop Zustand */
static int g_drag_win_id = -1;
static int g_drag_offset_x = 0;
static int g_drag_offset_y = 0;

static int g_dirty = 1;
static int g_dirty_x1 = 0, g_dirty_y1 = 0;
static int g_dirty_x2 = 0, g_dirty_y2 = 0;

void wm_mark_dirty(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    int x2 = x + w;
    int y2 = y + h;

    if (!g_dirty) {
        g_dirty_x1 = x;
        g_dirty_y1 = y;
        g_dirty_x2 = x2;
        g_dirty_y2 = y2;
        g_dirty = 1;
    } else {
        if (x < g_dirty_x1) g_dirty_x1 = x;
        if (y < g_dirty_y1) g_dirty_y1 = y;
        if (x2 > g_dirty_x2) g_dirty_x2 = x2;
        if (y2 > g_dirty_y2) g_dirty_y2 = y2;
    }

    if (g_dirty_x1 < 0) g_dirty_x1 = 0;
    if (g_dirty_y1 < 0) g_dirty_y1 = 0;
    if (g_dirty_x2 > (int)gop_width) g_dirty_x2 = (int)gop_width;
    if (g_dirty_y2 > (int)gop_height) g_dirty_y2 = (int)gop_height;
}

void wm_mark_all_dirty(void) {
    g_dirty_x1 = 0;
    g_dirty_y1 = 0;
    g_dirty_x2 = (int)gop_width;
    g_dirty_y2 = (int)gop_height;
    g_dirty = 1;
}

int wm_is_dirty(void) {
    return g_dirty;
}

void wm_get_dirty_bounds(int *dx, int *dy, int *dw, int *dh) {
    if (!g_dirty) {
        if (dx) *dx = 0;
        if (dy) *dy = 0;
        if (dw) *dw = 0;
        if (dh) *dh = 0;
        return;
    }
    if (dx) *dx = g_dirty_x1;
    if (dy) *dy = g_dirty_y1;
    if (dw) *dw = g_dirty_x2 - g_dirty_x1;
    if (dh) *dh = g_dirty_y2 - g_dirty_y1;
}

void wm_clear_dirty(void) {
    g_dirty = 0;
}

/* Drag & Drop Steuerung */
void wm_start_drag(int win_id, int mouse_x, int mouse_y) {
    if (win_id < 0 || win_id >= MAX_WINDOWS || g_windows[win_id].is_closed) return;
    g_drag_win_id = win_id;
    g_drag_offset_x = mouse_x - g_windows[win_id].x;
    g_drag_offset_y = mouse_y - g_windows[win_id].y;
}

void wm_update_drag(int mouse_x, int mouse_y) {
    if (g_drag_win_id < 0 || g_drag_win_id >= MAX_WINDOWS) return;
    Window *win = &g_windows[g_drag_win_id];
    if (win->is_closed) { g_drag_win_id = -1; return; }

    int old_x = win->x;
    int old_y = win->y;

    int new_x = mouse_x - g_drag_offset_x;
    int new_y = mouse_y - g_drag_offset_y;

    // Begrenzungen: Nicht über die Topbar (y=30) oder unter die Taskbar schieben
    if (new_y < 30) new_y = 30;
    if (new_y > (int)gop_height - 74) new_y = (int)gop_height - 74;
    if (new_x < 0) new_x = 0;
    if (new_x > (int)gop_width - win->width) new_x = (int)gop_width - win->width;

    if (new_x != old_x || new_y != old_y) {
        win->x = new_x;
        win->y = new_y;
        wm_mark_dirty(old_x - 4, old_y - 4, win->width + 12, win->height + 12);
        wm_mark_dirty(new_x - 4, new_y - 4, win->width + 12, win->height + 12);
    }
}

void wm_stop_drag(void) {
    g_drag_win_id = -1;
}

int wm_is_dragging(void) {
    return (g_drag_win_id >= 0);
}

UINT32 alpha_blend(UINT32 fg, UINT32 bg, UINT32 alpha) {
    if (alpha >= 255) return fg;
    if (alpha == 0) return bg;
    UINT32 inv = 255 - alpha;
    UINT32 r = (((fg >> 16) & 0xFF) * alpha + ((bg >> 16) & 0xFF) * inv) >> 8;
    UINT32 g = (((fg >> 8) & 0xFF) * alpha + ((bg >> 8) & 0xFF) * inv) >> 8;
    UINT32 b = ((fg & 0xFF) * alpha + (bg & 0xFF) * inv) >> 8;
    return (r << 16) | (g << 8) | b;
}

void wm_draw_text(const char *str, int x, int y, UINT32 fg_color, UINT32 bg_color) {
    int cur_x = x;
    int cur_y = y;

    for (int i = 0; str[i] != '\0'; i++) {
        char c = str[i];
        if (c == '\n') {
            cur_x = x;
            cur_y += 18;
            continue;
        }

        const unsigned char* glyph = font8x16[(unsigned char)c];
        for (int gy = 0; gy < 16; gy++) {
            unsigned char row = glyph[gy];
            for (int gx = 0; gx < 8; gx++) {
                int px = cur_x + gx;
                int py = cur_y + gy;

                if (px >= 0 && px < (int)gop_width && py >= 0 && py < (int)gop_height) {
                    if (row & (1 << (7 - gx))) {
                        put_pixel(px, py, fg_color);
                    } else if (bg_color != 0x00000000) {
                        put_pixel(px, py, bg_color);
                    }
                }
            }
        }
        cur_x += 8;
    }
}

void wm_draw_string_content(const char *str, int x, int y, int scale, UINT32 fg_color, UINT32 bg_color, int cx, int cy, int cw, int ch) {
    (void)scale; (void)cx; (void)cy; (void)cw; (void)ch;
    wm_draw_text(str, x, y, fg_color, bg_color);
}

void draw_rounded_rect_aa(int sx, int sy, int w, int h, int r, UINT32 color) {
    if (r <= 0) {
        draw_filled_rect(sx, sy, w, h, color);
        return;
    }
    int r2 = r * r;

    for (int y = 0; y < h; y++) {
        int py = sy + y;
        if (py < 0 || py >= (int)gop_height) continue;

        for (int x = 0; x < w; x++) {
            int px = sx + x;
            if (px < 0 || px >= (int)gop_width) continue;

            int in_corner = 0;
            int dx = 0, dy = 0;

            if (x < r && y < r) { dx = r - x; dy = r - y; in_corner = 1; }
            else if (x >= w - r && y < r) { dx = x - (w - r - 1); dy = r - y; in_corner = 1; }
            else if (x < r && y >= h - r) { dx = r - x; dy = y - (h - r - 1); in_corner = 1; }
            else if (x >= w - r && y >= h - r) { dx = x - (w - r - 1); dy = y - (h - r - 1); in_corner = 1; }

            if (!in_corner) {
                put_pixel(px, py, color);
            } else {
                int dist_sq = dx * dx + dy * dy;
                if (dist_sq <= (r - 1) * (r - 1)) {
                    put_pixel(px, py, color);
                } else if (dist_sq <= r2 + r) {
                    int diff = (r2 + r) - dist_sq;
                    UINT32 alpha = (diff * 255) / (2 * r);
                    if (alpha > 255) alpha = 255;
                    put_pixel(px, py, alpha_blend(color, get_pixel(px, py), alpha));
                }
            }
        }
    }
}

void draw_rounded_rect_gradient(int sx, int sy, int w, int h, int r, UINT32 top_col, UINT32 bot_col) {
    UINT32 tr = (top_col >> 16) & 0xFF, tg = (top_col >> 8) & 0xFF, tb = top_col & 0xFF;
    UINT32 br = (bot_col >> 16) & 0xFF, bg_val = (bot_col >> 8) & 0xFF, bb = bot_col & 0xFF;

    for (int y = 0; y < h; y++) {
        int py = sy + y;
        if (py < 0 || py >= (int)gop_height) continue;

        UINT32 cr = tr + ((br - tr) * y) / (h ? h : 1);
        UINT32 cg = tg + ((bg_val - tg) * y) / (h ? h : 1);
        UINT32 cb = tb + ((bb - tb) * y) / (h ? h : 1);
        UINT32 col = (cr << 16) | (cg << 8) | cb;

        for (int x = 0; x < w; x++) {
            int px = sx + x;
            if (px < 0 || px >= (int)gop_width) continue;

            int in_corner = 0;
            int dx = 0, dy = 0;

            if (x < r && y < r) { dx = r - x; dy = r - y; in_corner = 1; }
            else if (x >= w - r && y < r) { dx = x - (w - r - 1); dy = r - y; in_corner = 1; }
            else if (x < r && y >= h - r) { dx = r - x; dy = y - (h - r - 1); in_corner = 1; }
            else if (x >= w - r && y >= h - r) { dx = x - (w - r - 1); dy = y - (h - r - 1); in_corner = 1; }

            if (!in_corner) {
                put_pixel(px, py, col);
            } else {
                int dist_sq = dx * dx + dy * dy;
                if (dist_sq <= (r - 1) * (r - 1)) {
                    put_pixel(px, py, col);
                } else if (dist_sq <= r * r + r) {
                    int diff = (r * r + r) - dist_sq;
                    UINT32 alpha = (diff * 255) / (2 * r);
                    if (alpha > 255) alpha = 255;
                    put_pixel(px, py, alpha_blend(col, get_pixel(px, py), alpha));
                }
            }
        }
    }
}

void draw_line_aa(int x1, int y1, int x2, int y2, int thickness, UINT32 color) {
    int dx = x2 - x1, dy = y2 - y1;
    int steps = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    if (steps == 0) return;

    float x_inc = (float)dx / (float)steps;
    float y_inc = (float)dy / (float)steps;
    float cx = (float)x1, cy = (float)y1;

    for (int i = 0; i <= steps; i++) {
        int ix = (int)cx, iy = (int)cy;
        for (int tx = -thickness / 2; tx <= thickness / 2; tx++) {
            for (int ty = -thickness / 2; ty <= thickness / 2; ty++) {
                int px = ix + tx, py = iy + ty;
                if (px >= 0 && px < (int)gop_width && py >= 0 && py < (int)gop_height) {
                    put_pixel(px, py, color);
                }
            }
        }
        cx += x_inc;
        cy += y_inc;
    }
}

void draw_circle_aa(int cx, int cy, int radius, int thickness, UINT32 color) {
    int r_in = radius - thickness / 2;
    int r_out = radius + thickness / 2;
    int r_in_sq = r_in * r_in;
    int r_out_sq = r_out * r_out;

    for (int y = -r_out - 1; y <= r_out + 1; y++) {
        int py = cy + y;
        if (py < 0 || py >= (int)gop_height) continue;

        for (int x = -r_out - 1; x <= r_out + 1; x++) {
            int px = cx + x;
            if (px < 0 || px >= (int)gop_width) continue;

            int d2 = x * x + y * y;
            if (d2 >= r_in_sq && d2 <= r_out_sq) {
                put_pixel(px, py, color);
            }
        }
    }
}

void wm_init(void) {
    g_window_count = 0;
    g_active_win_id = -1;
    g_drag_win_id = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        g_windows[i].is_closed = 1;
        g_windows[i].on_click = NULL;
        g_windows[i].on_key = NULL;
    }
    wm_mark_all_dirty();
}

int wm_create_window(int x, int y, int w, int h, const char *title, void (*on_paint)(int, int, int, int, int)) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!g_windows[i].is_closed) {
            int match = 1, p = 0;
            while (title[p] || g_windows[i].title[p]) {
                if (title[p] != g_windows[i].title[p]) { match = 0; break; }
                p++;
            }
            if (match) {
                wm_focus_window(i);
                return i;
            }
        }
    }

    if (g_window_count >= MAX_WINDOWS) return -1;

    int slot = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].is_closed) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return -1;

    Window *win = &g_windows[slot];
    win->id = slot;
    win->x = x;
    win->y = y;
    win->width = w;
    win->height = h;
    win->is_active = 1;
    win->is_minimized = 0;
    win->is_closed = 0;
    win->bg_color = 0x000F172A;
    win->on_paint = on_paint;
    win->on_click = NULL;
    win->on_key = NULL;

    int p = 0;
    while (title[p] && p < 63) {
        win->title[p] = title[p];
        p++;
    }
    win->title[p] = '\0';

    wm_focus_window(slot);
    g_window_count++;
    wm_mark_all_dirty();
    return slot;
}

int wm_create_window_auto(const char *title, int content_w, int content_h, void (*on_paint)(int, int, int, int, int)) {
    int total_w = content_w + 36;
    if (total_w < 320) total_w = 320;
    int total_h = 36 + content_h + 28;

    int x = ((int)gop_width - total_w) / 2;
    int y = ((int)gop_height - 44 - total_h) / 2;
    if (y < 36) y = 36;

    return wm_create_window(x, y, total_w, total_h, title, on_paint);
}

int wm_create_window_for_lines(const char *title, const char **lines, int line_count, int scale, void (*on_paint)(int, int, int, int, int)) {
    (void)scale;
    int max_len = 0;
    for (int i = 0; i < line_count; i++) {
        if (!lines[i]) continue;
        int len = 0;
        while (lines[i][len] != '\0') len++;
        if (len > max_len) max_len = len;
    }
    return wm_create_window_auto(title, max_len * 8, line_count * 18, on_paint);
}

void wm_close_window(int id) {
    if (id < 0 || id >= MAX_WINDOWS || g_windows[id].is_closed) return;
    g_windows[id].is_closed = 1;
    g_window_count--;
    if (g_drag_win_id == id) g_drag_win_id = -1;
    if (g_active_win_id == id) wm_focus_next();
    wm_mark_all_dirty();
}

void wm_close_active(void) {
    if (g_active_win_id >= 0) wm_close_window(g_active_win_id);
}

void wm_minimize_window(int id) {
    if (id < 0 || id >= MAX_WINDOWS || g_windows[id].is_closed) return;
    g_windows[id].is_minimized = !g_windows[id].is_minimized;
    if (g_drag_win_id == id) g_drag_win_id = -1;
    if (g_windows[id].is_minimized && g_active_win_id == id) wm_focus_next();
    wm_mark_all_dirty();
}

void wm_minimize_active(void) {
    if (g_active_win_id >= 0) wm_minimize_window(g_active_win_id);
}

void wm_focus_window(int id) {
    if (id < 0 || id >= MAX_WINDOWS || g_windows[id].is_closed) return;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        g_windows[i].is_active = (i == id);
    }
    g_windows[id].is_minimized = 0;
    g_active_win_id = id;
    wm_mark_all_dirty();
}

void wm_focus_next(void) {
    if (g_window_count <= 0) { g_active_win_id = -1; return; }
    int current = (g_active_win_id >= 0) ? g_active_win_id : 0;
    for (int i = 1; i <= MAX_WINDOWS; i++) {
        int next = (current + i) % MAX_WINDOWS;
        if (!g_windows[next].is_closed && !g_windows[next].is_minimized) {
            wm_focus_window(next);
            return;
        }
    }
}

Window* wm_get_window(int id) {
    if (id < 0 || id >= MAX_WINDOWS || g_windows[id].is_closed) return NULL;
    return &g_windows[id];
}

int wm_get_window_count(void) { return g_window_count; }
int wm_get_active_window_id(void) { return g_active_win_id; }

/*
 * Rendert das Fenster mit klaren Konturen und hohem Kontrast
 */
static void render_window_frame(Window *win) {
    if (win->is_closed || win->is_minimized) return;

    int x = win->x, y = win->y, w = win->width, h = win->height;

    // Drop Shadow
    draw_rounded_rect_aa(x + 4, y + 4, w, h, 8, 0x00020617);
    
    // Äußerer Kontrast-Border
    UINT32 border_col = win->is_active ? 0x0060A5FA : 0x00334155;
    draw_rounded_rect_aa(x, y, w, h, 8, border_col);
    draw_rounded_rect_aa(x + 1, y + 1, w - 2, h - 2, 7, win->bg_color);

    // Titelleiste mit brillantem Farbverlauf
    UINT32 tb_top = win->is_active ? 0x002563EB : 0x001E293B;
    UINT32 tb_bot = win->is_active ? 0x001D4ED8 : 0x000F172A;
    draw_rounded_rect_gradient(x + 2, y + 2, w - 4, 32, 6, tb_top, tb_bot);

    // Trennlinie unter der Titelleiste
    draw_filled_rect(x + 2, y + 33, w - 4, 1, win->is_active ? 0x001E40AF : 0x00334155);

    // Titelleistentext - Reinweiß und glasklar
    UINT32 title_fg = win->is_active ? 0x00FFFFFF : 0x0094A3B8;
    wm_draw_text(win->title, x + 14, y + 9, title_fg, 0x00000000);

    // Schließen-Button (Leuchtendes Rot mit weißem X)
    draw_rounded_rect_gradient(x + w - 28, y + 6, 22, 22, 5, 0x00EF4444, 0x00DC2626);
    draw_rounded_rect_aa(x + w - 28, y + 6, 22, 22, 5, 0x00FCA5A5);
    draw_rounded_rect_gradient(x + w - 27, y + 7, 20, 20, 4, 0x00EF4444, 0x00DC2626);
    wm_draw_text("X", x + w - 21, y + 9, 0x00FFFFFF, 0x00000000);

    // Minimieren-Button (Schiefergrau mit weißem -)
    draw_rounded_rect_gradient(x + w - 54, y + 6, 22, 22, 5, 0x00475569, 0x00334155);
    draw_rounded_rect_aa(x + w - 54, y + 6, 22, 22, 5, 0x0094A3B8);
    draw_rounded_rect_gradient(x + w - 53, y + 7, 20, 20, 4, 0x00475569, 0x00334155);
    wm_draw_text("-", x + w - 47, y + 9, 0x00FFFFFF, 0x00000000);

    // Content-Callback (Inhalt der App)
    if (win->on_paint) {
        win->on_paint(win->id, x + 12, y + 40, w - 24, h - 48);
    }
}

void wm_render_all(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!g_windows[i].is_closed && !g_windows[i].is_minimized && !g_windows[i].is_active) {
            render_window_frame(&g_windows[i]);
        }
    }
    if (g_active_win_id >= 0 && !g_windows[g_active_win_id].is_closed && !g_windows[g_active_win_id].is_minimized) {
        render_window_frame(&g_windows[g_active_win_id]);
    }
}
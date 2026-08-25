#include "wm.h"
#include "font.h"

extern UINTN gop_width;
extern UINTN gop_height;

void put_pixel(UINTN x, UINTN y, UINT32 color);
UINT32 get_pixel(UINTN x, UINTN y);
void draw_filled_rect(UINTN start_x, UINTN start_y, UINTN width, UINTN height, UINT32 color);

static Window g_windows[MAX_WINDOWS];
static UINT32 *g_win_surfaces[MAX_WINDOWS] = {0};
static int g_window_count = 0;
static int g_active_win_id = -1;
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
        g_dirty_x1 = x; g_dirty_y1 = y;
        g_dirty_x2 = x2; g_dirty_y2 = y2;
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

int wm_is_dirty(void) { return g_dirty; }

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

void wm_clear_dirty(void) { g_dirty = 0; }

void wm_start_drag(int win_id, int mouse_x, int mouse_y) {
    if (win_id < 0 || win_id >= MAX_WINDOWS || g_windows[win_id].is_closed) return;
    Window *win = &g_windows[win_id];
    
    if (win->is_maximized) {
        wm_maximize_window(win_id);
        win->x = mouse_x - win->width / 2;
        win->y = mouse_y - 15;
    }

    g_drag_win_id = win_id;
    g_drag_offset_x = mouse_x - win->x;
    g_drag_offset_y = mouse_y - win->y;
}

void wm_update_drag(int mouse_x, int mouse_y) {
    if (g_drag_win_id < 0 || g_drag_win_id >= MAX_WINDOWS) return;
    Window *win = &g_windows[g_drag_win_id];
    if (win->is_closed) { g_drag_win_id = -1; return; }

    int old_x = win->x;
    int old_y = win->y;
    int new_x = mouse_x - g_drag_offset_x;
    int new_y = mouse_y - g_drag_offset_y;

    if (new_y < 0) new_y = 0;
    if (new_y > (int)gop_height - 60) new_y = (int)gop_height - 60;
    if (new_x < 0) new_x = 0;
    if (new_x > (int)gop_width - win->width) new_x = (int)gop_width - win->width;

    if (new_x != old_x || new_y != old_y) {
        win->x = new_x;
        win->y = new_y;
        wm_mark_dirty(old_x - 4, old_y - 4, win->width + 12, win->height + 12);
        wm_mark_dirty(new_x - 4, new_y - 4, win->width + 12, win->height + 12);
    }
}

void wm_stop_drag(void) { g_drag_win_id = -1; }
int wm_is_dragging(void) { return (g_drag_win_id >= 0); }

UINT32 alpha_blend(UINT32 fg, UINT32 bg, UINT32 alpha) {
    if (alpha >= 255) return fg;
    if (alpha == 0) return bg;
    UINT32 inv = 255 - alpha;
    UINT32 r = (((fg >> 16) & 0xFF) * alpha + ((bg >> 16) & 0xFF) * inv) >> 8;
    UINT32 g = (((fg >> 8) & 0xFF) * alpha + ((bg >> 8) & 0xFF) * inv) >> 8;
    UINT32 b = ((fg & 0xFF) * alpha + (bg & 0xFF) * inv) >> 8;
    return (r << 16) | (g << 8) | b;
}

void draw_line_aa(int x1, int y1, int x2, int y2, int thickness, UINT32 color) {
    int dx = (x2 >= x1) ? (x2 - x1) : (x1 - x2);
    int dy = (y2 >= y1) ? (y2 - y1) : (y1 - y2); // KORRIGIERT: y1 - y2
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2;
    int th = thickness / 2;

    int cur_x = x1, cur_y = y1;
    while (1) {
        for (int tx = -th; tx <= th; tx++) {
            for (int ty = -th; ty <= th; ty++) {
                int px = cur_x + tx;
                int py = cur_y + ty;
                if (px >= 0 && px < (int)gop_width && py >= 0 && py < (int)gop_height) {
                    put_pixel(px, py, color);
                }
            }
        }

        if (cur_x == x2 && cur_y == y2) break;

        int e2 = err;
        if (e2 > -dx) {
            err -= dy;
            cur_x += sx;
        }
        if (e2 < dy) {
            err += dx;
            cur_y += sy;
        }
    }
}

void wm_draw_text_scaled(const char *str, int x, int y, int scale, UINT32 fg_color, UINT32 bg_color) {
    if (!str || scale <= 0) return;
    int cur_x = x;
    int cur_y = y;

    for (int i = 0; str[i] != '\0'; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c == '\n') {
            cur_x = x;
            cur_y += 18 * scale;
            continue;
        }

        const unsigned char* glyph = font8x16[c];
        for (int gy = 0; gy < 16; gy++) {
            unsigned char row = glyph[gy];
            for (int gx = 0; gx < 8; gx++) {
                int bit = (row & (1 << (7 - gx)));
                UINT32 col = bit ? fg_color : bg_color;

                if (bit || bg_color != 0x00000000) {
                    for (int sy = 0; sy < scale; sy++) {
                        for (int sx = 0; sx < scale; sx++) {
                            int px = cur_x + (gx * scale) + sx;
                            int py = cur_y + (gy * scale) + sy;
                            if (px >= 0 && px < (int)gop_width && py >= 0 && py < (int)gop_height) {
                                put_pixel(px, py, col);
                            }
                        }
                    }
                }
            }
        }
        cur_x += 8 * scale;
    }
}

void wm_draw_text_shadow(const char *str, int x, int y, int scale, UINT32 fg_color, UINT32 shadow_color) {
    wm_draw_text_scaled(str, x + 1, y + 1, scale, shadow_color, 0x00000000);
    wm_draw_text_scaled(str, x, y, scale, fg_color, 0x00000000);
}

void wm_draw_text(const char *str, int x, int y, UINT32 fg_color, UINT32 bg_color) {
    wm_draw_text_scaled(str, x, y, 1, fg_color, bg_color);
}

void wm_draw_text_wrapped(const char *str, int x, int y, int max_w, int max_h, UINT32 fg_color, UINT32 bg_color) {
    if (!str || max_w < 8 || max_h < 16) return;

    int max_chars_per_line = max_w / 8;
    int cur_y = y;
    int i = 0;

    while (str[i] != '\0' && (cur_y + 16 <= y + max_h)) {
        if (str[i] == '\n') {
            cur_y += 18;
            i++;
            continue;
        }

        int line_len = 0;
        int last_space = -1;

        while (str[i + line_len] != '\0' && str[i + line_len] != '\n' && line_len < max_chars_per_line) {
            if (str[i + line_len] == ' ') {
                last_space = line_len;
            }
            line_len++;
        }

        if (str[i + line_len] == '\0' || str[i + line_len] == '\n') {
            char buf[128];
            int c = 0;
            while (c < line_len && c < 127) { buf[c] = str[i + c]; c++; }
            buf[c] = '\0';
            wm_draw_text(buf, x, cur_y, fg_color, bg_color);
            i += line_len;
            if (str[i] == '\n') { i++; }
            cur_y += 18;
        } else {
            int break_at = (last_space > 0) ? last_space : max_chars_per_line;
            if (break_at <= 0) break_at = 1;
            char buf[128];
            int c = 0;
            while (c < break_at && c < 127) { buf[c] = str[i + c]; c++; }
            buf[c] = '\0';
            wm_draw_text(buf, x, cur_y, fg_color, bg_color);
            i += break_at;
            if (str[i] == ' ') i++;
            cur_y += 18;
        }
    }
}

void draw_rounded_rect_aa(int sx, int sy, int w, int h, int r, UINT32 color) {
    if (w <= 0 || h <= 0) return;
    if (r <= 0) { draw_filled_rect(sx, sy, w, h, color); return; }
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

            if (!in_corner || (dx * dx + dy * dy <= r2)) {
                put_pixel(px, py, color);
            }
        }
    }
}

void draw_rounded_rect_gradient(int sx, int sy, int w, int h, int r, UINT32 top_col, UINT32 bot_col) {
    if (w <= 0 || h <= 0) return;
    int tr = (int)((top_col >> 16) & 0xFF), tg = (int)((top_col >> 8) & 0xFF), tb = (int)(top_col & 0xFF);
    int br = (int)((bot_col >> 16) & 0xFF), bg_val = (int)((bot_col >> 8) & 0xFF), bb = (int)(bot_col & 0xFF);
    int r2 = r * r;

    for (int y = 0; y < h; y++) {
        int py = sy + y;
        if (py < 0 || py >= (int)gop_height) continue;

        int cr = tr + ((br - tr) * y) / (h > 1 ? (h - 1) : 1);
        int cg = tg + ((bg_val - tg) * y) / (h > 1 ? (h - 1) : 1);
        int cb = tb + ((bb - tb) * y) / (h > 1 ? (h - 1) : 1);

        UINT32 col = ((UINT32)cr << 16) | ((UINT32)cg << 8) | (UINT32)cb;

        for (int x = 0; x < w; x++) {
            int px = sx + x;
            if (px < 0 || px >= (int)gop_width) continue;

            int in_corner = 0;
            int dx = 0, dy = 0;

            if (r > 0) {
                if (x < r && y < r) { dx = r - x; dy = r - y; in_corner = 1; }
                else if (x >= w - r && y < r) { dx = x - (w - r - 1); dy = r - y; in_corner = 1; }
                else if (x < r && y >= h - r) { dx = r - x; dy = y - (h - r - 1); in_corner = 1; }
                else if (x >= w - r && y >= h - r) { dx = x - (w - r - 1); dy = y - (h - r - 1); in_corner = 1; }
            }

            if (!in_corner || (dx * dx + dy * dy <= r2)) {
                put_pixel(px, py, col);
            }
        }
    }
}

/* ====================================================
 * WINDOW SURFACE ZEICHENFUNKTIONEN
 * ==================================================== */
void wm_surface_clear(int win_id, UINT32 color) {
    Window *win = wm_get_window(win_id);
    if (!win || !win->surface) return;

    int cw = win->is_maximized ? win->width : win->width - 12;
    int ch = win->is_maximized ? win->height - 30 : win->height - 36;
    if (cw < 0) cw = 0;
    if (ch < 0) ch = 0;

    for (int y = 0; y < ch && y < SURFACE_HEIGHT; y++) {
        for (int x = 0; x < cw && x < SURFACE_STRIDE; x++) {
            win->surface[y * SURFACE_STRIDE + x] = color;
        }
    }
    wm_mark_all_dirty();
}

void wm_surface_draw_rect(int win_id, int x, int y, int w, int h, UINT32 color) {
    Window *win = wm_get_window(win_id);
    if (!win || !win->surface) return;

    int max_w = win->is_maximized ? win->width : win->width - 12;
    int max_h = win->is_maximized ? win->height - 30 : win->height - 36;

    for (int sy = y; sy < y + h; sy++) {
        if (sy < 0 || sy >= max_h || sy >= SURFACE_HEIGHT) continue;
        for (int sx = x; sx < x + w; sx++) {
            if (sx < 0 || sx >= max_w || sx >= SURFACE_STRIDE) continue;
            win->surface[sy * SURFACE_STRIDE + sx] = color;
        }
    }
    wm_mark_all_dirty();
}

void wm_surface_draw_gradient(int win_id, int x, int y, int w, int h, UINT32 top_col, UINT32 bot_col) {
    Window *win = wm_get_window(win_id);
    if (!win || !win->surface || w <= 0 || h <= 0) return;

    int max_w = win->is_maximized ? win->width : win->width - 12;
    int max_h = win->is_maximized ? win->height - 30 : win->height - 36;

    int tr = (int)((top_col >> 16) & 0xFF), tg = (int)((top_col >> 8) & 0xFF), tb = (int)(top_col & 0xFF);
    int br = (int)((bot_col >> 16) & 0xFF), bg_val = (int)((bot_col >> 8) & 0xFF), bb = (int)(bot_col & 0xFF);

    for (int cy = 0; cy < h; cy++) {
        int py = y + cy;
        if (py < 0 || py >= max_h || py >= SURFACE_HEIGHT) continue;

        int cr = tr + ((br - tr) * cy) / (h > 1 ? (h - 1) : 1);
        int cg = tg + ((bg_val - tg) * cy) / (h > 1 ? (h - 1) : 1);
        int cb = tb + ((bb - tb) * cy) / (h > 1 ? (h - 1) : 1);

        UINT32 col = ((UINT32)cr << 16) | ((UINT32)cg << 8) | (UINT32)cb;

        for (int cx = 0; cx < w; cx++) {
            int px = x + cx;
            if (px < 0 || px >= max_w || px >= SURFACE_STRIDE) continue;
            win->surface[py * SURFACE_STRIDE + px] = col;
        }
    }
    wm_mark_all_dirty();
}

void wm_surface_draw_text(int win_id, const char *str, int x, int y, UINT32 color) {
    Window *win = wm_get_window(win_id);
    if (!win || !win->surface || !str) return;

    int max_w = win->is_maximized ? win->width : win->width - 12;
    int max_h = win->is_maximized ? win->height - 30 : win->height - 36;

    int cur_x = x;
    int cur_y = y;

    for (int i = 0; str[i] != '\0'; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c == '\n') {
            cur_x = x;
            cur_y += 18;
            continue;
        }

        const unsigned char *glyph = font8x16[c];
        for (int gy = 0; gy < 16; gy++) {
            unsigned char row = glyph[gy];
            for (int gx = 0; gx < 8; gx++) {
                if (row & (1 << (7 - gx))) {
                    int px = cur_x + gx;
                    int py = cur_y + gy;
                    if (px >= 0 && px < max_w && px < SURFACE_STRIDE && py >= 0 && py < max_h && py < SURFACE_HEIGHT) {
                        win->surface[py * SURFACE_STRIDE + px] = color;
                    }
                }
            }
        }
        cur_x += 8;
    }
    wm_mark_all_dirty();
}

void wm_init(void) {
    g_window_count = 0;
    g_active_win_id = -1;
    g_drag_win_id = -1;

    UINTN surf_bytes = SURFACE_STRIDE * SURFACE_HEIGHT * sizeof(UINT32);

    for (int i = 0; i < MAX_WINDOWS; i++) {
        g_windows[i].is_closed = 1;
        g_windows[i].on_click = NULL;
        g_windows[i].on_key = NULL;

        if (!g_win_surfaces[i]) {
            g_win_surfaces[i] = (UINT32*)AllocateZeroPool(surf_bytes);
            if (!g_win_surfaces[i]) {
                UINTN pages_per_win = EFI_SIZE_TO_PAGES(surf_bytes);
                EFI_PHYSICAL_ADDRESS phys = 0;
                if (BS && BS->AllocatePages) {
                    EFI_STATUS status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, pages_per_win, &phys);
                    if (status == EFI_SUCCESS && phys != 0) {
                        g_win_surfaces[i] = (UINT32*)(UINTN)phys;
                    }
                }
            }
        }
        g_windows[i].surface = g_win_surfaces[i];
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
        if (g_windows[i].is_closed) { slot = i; break; }
    }
    if (slot == -1) return -1;

    Window *win = &g_windows[slot];
    win->id = slot;
    win->x = x;
    win->y = y;
    win->width = w;
    win->height = h;
    win->orig_x = x;
    win->orig_y = y;
    win->orig_w = w;
    win->orig_h = h;
    win->is_active = 1;
    win->is_minimized = 0;
    win->is_maximized = 0;
    win->is_closed = 0;
    win->is_dirty_content = 1;
    win->bg_color = 0x00F8FAFC;
    win->surface = g_win_surfaces[slot];
    win->on_paint = on_paint;
    win->on_click = NULL;
    win->on_key = NULL;

    int cw = w - 12;
    int ch = h - 36;
    if (win->surface) {
        for (int py = 0; py < ch && py < SURFACE_HEIGHT; py++) {
            for (int px = 0; px < cw && px < SURFACE_STRIDE; px++) {
                win->surface[py * SURFACE_STRIDE + px] = win->bg_color;
            }
        }
    }

    int p = 0;
    while (title[p] && p < 63) { win->title[p] = title[p]; p++; }
    win->title[p] = '\0';

    wm_focus_window(slot);
    g_window_count++;
    wm_mark_all_dirty();
    return slot;
}

int wm_create_window_auto(const char *title, int content_w, int content_h, void (*on_paint)(int, int, int, int, int)) {
    int total_w = content_w + 12;
    if (total_w < 260) total_w = 260;
    int total_h = content_h + 36;

    int x = ((int)gop_width - total_w) / 2;
    int y = ((int)gop_height - 40 - total_h) / 2;
    if (y < 20) y = 20;

    return wm_create_window(x, y, total_w, total_h, title, on_paint);
}

void wm_close_window(int id) {
    if (id < 0 || id >= MAX_WINDOWS || g_windows[id].is_closed) return;
    g_windows[id].is_closed = 1;
    g_window_count--;
    if (g_drag_win_id == id) g_drag_win_id = -1;
    wm_mark_all_dirty();
}

void wm_minimize_window(int id) {
    if (id < 0 || id >= MAX_WINDOWS || g_windows[id].is_closed) return;
    g_windows[id].is_minimized = !g_windows[id].is_minimized;
    wm_mark_all_dirty();
}

void wm_maximize_window(int id) {
    if (id < 0 || id >= MAX_WINDOWS || g_windows[id].is_closed) return;
    Window *win = &g_windows[id];

    int new_w, new_h;

    if (!win->is_maximized) {
        win->orig_x = win->x;
        win->orig_y = win->y;
        win->orig_w = win->width;
        win->orig_h = win->height;

        win->x = 0;
        win->y = 0;
        new_w = (int)gop_width;
        new_h = (int)gop_height - 38;
        win->is_maximized = 1;
    } else {
        win->x = win->orig_x;
        win->y = win->orig_y;
        new_w = win->orig_w;
        new_h = win->orig_h;
        win->is_maximized = 0;
    }

    win->width = new_w;
    win->height = new_h;
    win->is_dirty_content = 1;

    wm_surface_clear(id, win->bg_color);

    if (win->on_paint) {
        int off_x = win->is_maximized ? 0 : 6;
        int off_y = 30;
        int cw = win->is_maximized ? win->width : win->width - 12;
        int ch = win->is_maximized ? win->height - 30 : win->height - 36;
        win->on_paint(win->id, win->x + off_x, win->y + off_y, cw, ch);
    }

    wm_mark_all_dirty();
}

void wm_maximize_active(void) {
    if (g_active_win_id >= 0) {
        wm_maximize_window(g_active_win_id);
    }
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

Window* wm_get_window(int id) {
    if (id < 0 || id >= MAX_WINDOWS || g_windows[id].is_closed) return NULL;
    return &g_windows[id];
}

int wm_get_active_window_id(void) { return g_active_win_id; }

static void render_window_frame(Window *win) {
    if (win->is_closed || win->is_minimized) return;

    int x = win->x, y = win->y, w = win->width, h = win->height;

    if (!win->is_maximized) {
        draw_rounded_rect_aa(x + 5, y + 5, w, h, 6, 0x0002060C);
        UINT32 aero_border = win->is_active ? 0x004A8FA8 : 0x002A3D48;
        draw_rounded_rect_aa(x, y, w, h, 6, aero_border);
    }

    UINT32 top_glass = win->is_active ? 0x001B485A : 0x0012242D;
    UINT32 bot_glass = win->is_active ? 0x000B2430 : 0x0008141A;
    int title_r = win->is_maximized ? 0 : 5;
    draw_rounded_rect_gradient(x, y, w, 30, title_r, top_glass, bot_glass);
    draw_filled_rect(x, y + 15, w, 1, win->is_active ? 0x003A7088 : 0x00223D48);

    wm_draw_text_shadow(win->title, x + 10, y + 7, 1, 0x00FFFFFF, 0x0002060C);

    int btn_top = win->is_maximized ? 4 : 5;

    draw_rounded_rect_gradient(x + w - 30, y + btn_top, 26, 20, 4, 0x00E04040, 0x00A01818);
    draw_rounded_rect_aa(x + w - 30, y + btn_top, 26, 20, 4, 0x00FFA0A0);
    wm_draw_text("X", x + w - 21, y + btn_top + 2, 0x00FFFFFF, 0x00000000);

    draw_rounded_rect_gradient(x + w - 58, y + btn_top, 26, 20, 4, 0x00284858, 0x00142834);
    draw_rounded_rect_aa(x + w - 58, y + btn_top, 26, 20, 4, 0x005AC0E0);
    wm_draw_text(win->is_maximized ? "^" : "O", x + w - 49, y + btn_top + 2, 0x00FFFFFF, 0x00000000);

    draw_rounded_rect_gradient(x + w - 86, y + btn_top, 26, 20, 4, 0x00284858, 0x00142834);
    draw_rounded_rect_aa(x + w - 86, y + btn_top, 26, 20, 4, 0x004A8FA8);
    wm_draw_text("-", x + w - 77, y + btn_top + 2, 0x00FFFFFF, 0x00000000);

    int off_x = win->is_maximized ? 0 : 6;
    int off_y = 30;
    int cw = win->is_maximized ? win->width : win->width - 12;
    int ch = win->is_maximized ? win->height - 30 : win->height - 36;

    if (win->surface && cw > 0 && ch > 0) {
        if (cw > SURFACE_STRIDE) cw = SURFACE_STRIDE;
        if (ch > SURFACE_HEIGHT) ch = SURFACE_HEIGHT;
        for (int cy = 0; cy < ch; cy++) {
            for (int cx = 0; cx < cw; cx++) {
                put_pixel(x + off_x + cx, y + off_y + cy, win->surface[cy * SURFACE_STRIDE + cx]);
            }
        }
    }

    if (win->on_paint) {
        win->on_paint(win->id, x + off_x, y + off_y, cw, ch);
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
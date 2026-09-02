#include "wm.h"
#include "font.h"
#include <velo/icons.h>

extern UINTN gop_width;
extern UINTN gop_height;
extern UINT32 *g_backbuffer;

void put_pixel(UINTN x, UINTN y, UINT32 color);
UINT32 get_pixel(UINTN x, UINTN y);
void draw_filled_rect(UINTN start_x, UINTN start_y, UINTN width, UINTN height, UINT32 color);

static Window g_windows[MAX_WINDOWS];
static UINT32 *g_win_surfaces[MAX_WINDOWS] = {0};
static int g_window_count = 0;
static int g_active_win_id = -1;

static int g_z_order[MAX_WINDOWS];
static int g_z_count = 0;

static int g_drag_win_id = -1;
static int g_drag_offset_x = 0;
static int g_drag_offset_y = 0;

static int g_dirty = 1;
static int g_dirty_x1 = 0, g_dirty_y1 = 0;
static int g_dirty_x2 = 0, g_dirty_y2 = 0;

void wm_mark_dirty(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    int x2 = x + w, y2 = y + h;
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
    g_dirty_x1 = 0; g_dirty_y1 = 0;
    g_dirty_x2 = (int)gop_width; g_dirty_y2 = (int)gop_height;
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

void wm_window_push_key(int win_id, char key) {
    if (win_id < 0 || win_id >= MAX_WINDOWS || g_windows[win_id].is_closed) return;
    Window *win = &g_windows[win_id];
    int next = (win->key_q_head + 1) % 16;
    if (next != win->key_q_tail) {
        win->key_queue[win->key_q_head] = key;
        win->key_q_head = next;
    }
}

char wm_window_pop_key(int win_id) {
    if (win_id < 0 || win_id >= MAX_WINDOWS || g_windows[win_id].is_closed) return 0;
    Window *win = &g_windows[win_id];
    if (win->key_q_head == win->key_q_tail) return 0;
    char k = win->key_queue[win->key_q_tail];
    win->key_q_tail = (win->key_q_tail + 1) % 16;
    return k;
}

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
    wm_focus_window(win_id);
}

void wm_update_drag(int mouse_x, int mouse_y) {
    if (g_drag_win_id < 0 || g_drag_win_id >= MAX_WINDOWS) return;
    Window *win = &g_windows[g_drag_win_id];
    if (win->is_closed) { g_drag_win_id = -1; return; }

    int old_x = win->x, old_y = win->y;
    int new_x = mouse_x - g_drag_offset_x;
    int new_y = mouse_y - g_drag_offset_y;

    if (new_y < 0) new_y = 0;
    if (new_y > (int)gop_height - 50) new_y = (int)gop_height - 50;
    if (new_x < 0) new_x = 0;
    if (new_x > (int)gop_width - win->width) new_x = (int)gop_width - win->width;

    if (new_x != old_x || new_y != old_y) {
        win->x = new_x; win->y = new_y;
        wm_mark_dirty(old_x - 8, old_y - 8, win->width + 20, win->height + 20);
        wm_mark_dirty(new_x - 8, new_y - 8, win->width + 20, win->height + 20);
    }
}

void wm_stop_drag(void) { g_drag_win_id = -1; }
int  wm_is_dragging(void) { return (g_drag_win_id >= 0); }

UINT32 alpha_blend(UINT32 fg, UINT32 bg, UINT32 alpha) {
    if (alpha >= 255) return fg;
    if (alpha == 0) return bg;
    UINT32 inv = 255 - alpha;
    UINT32 rb = (((fg & 0x00FF00FF) * alpha + (bg & 0x00FF00FF) * inv) >> 8) & 0x00FF00FF;
    UINT32 g  = (((fg & 0x0000FF00) * alpha + (bg & 0x0000FF00) * inv) >> 8) & 0x0000FF00;
    return rb | g;
}

static void draw_shadow_box(int sx, int sy, int w, int h, int radius, int blur) {
    for (int b = blur; b > 0; b--) {
        UINT32 alpha = 40 / (b + 1);
        int ox = sx - b, oy = sy - b + 2;
        int ow = w + b * 2, oh = h + b * 2;
        int r2 = (radius + b) * (radius + b);

        for (int y = 0; y < oh; y++) {
            int py = oy + y;
            if (py < 0 || py >= (int)gop_height) continue;
            int dy = (y < radius + b) ? (radius + b - y) : ((y >= oh - radius - b) ? (y - (oh - radius - b - 1)) : 0);
            int dy2 = dy * dy;

            for (int x = 0; x < ow; x++) {
                int px = ox + x;
                if (px < 0 || px >= (int)gop_width) continue;
                if (px >= sx && px < sx + w && py >= sy && py < sy + h) continue;

                int dx = (x < radius + b) ? (radius + b - x) : ((x >= ow - radius - b) ? (x - (ow - radius - b - 1)) : 0);
                if (dx * dx + dy2 <= r2) {
                    UINT32 bg = get_pixel((UINTN)px, (UINTN)py);
                    put_pixel((UINTN)px, (UINTN)py, alpha_blend(0x00000000, bg, alpha));
                }
            }
        }
    }
}

void draw_line_aa(int x1, int y1, int x2, int y2, int thickness, UINT32 color) {
    int dx = (x2 >= x1) ? (x2 - x1) : (x1 - x2);
    int dy = (y2 >= y1) ? (y2 - y1) : (y1 - y2);
    int sx = (x1 < x2) ? 1 : -1, sy = (y1 < y2) ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2;
    int th = thickness / 2;

    int cur_x = x1, cur_y = y1;
    while (1) {
        for (int tx = -th; tx <= th; tx++) {
            for (int ty = -th; ty <= th; ty++) {
                int px = cur_x + tx, py = cur_y + ty;
                if (px >= 0 && px < (int)gop_width && py >= 0 && py < (int)gop_height) {
                    put_pixel((UINTN)px, (UINTN)py, color);
                }
            }
        }
        if (cur_x == x2 && cur_y == y2) break;
        int e2 = err;
        if (e2 > -dx) { err -= dy; cur_x += sx; }
        if (e2 < dy)  { err += dx; cur_y += sy; }
    }
}

void draw_rounded_rect_gradient(int sx, int sy, int w, int h, int r, UINT32 top_col, UINT32 bot_col) {
    if (w <= 0 || h <= 0) return;
    int tr = (top_col >> 16) & 0xFF, tg = (top_col >> 8) & 0xFF, tb = top_col & 0xFF;
    int br = (bot_col >> 16) & 0xFF, bg_v = (bot_col >> 8) & 0xFF, bb = bot_col & 0xFF;
    int r2 = r * r;
    int denom = (h > 1 ? (h - 1) : 1);

    for (int y = 0; y < h; y++) {
        int py = sy + y;
        if (py < 0 || py >= (int)gop_height) continue;

        int cr = tr + ((br - tr) * y) / denom;
        int cg = tg + ((bg_v - tg) * y) / denom;
        int cb = tb + ((bb - tb) * y) / denom;
        UINT32 col = ((UINT32)cr << 16) | ((UINT32)cg << 8) | (UINT32)cb;

        int is_corner = (r > 0) && ((y < r) || (y >= h - r));

        if (!is_corner) {
            for (int x = 0; x < w; x++) {
                int px = sx + x;
                if (px >= 0 && px < (int)gop_width) put_pixel((UINTN)px, (UINTN)py, col);
            }
        } else {
            int dy = (y < r) ? (r - y) : (y - (h - r - 1));
            int dy2 = dy * dy;
            for (int x = 0; x < w; x++) {
                int px = sx + x;
                if (px < 0 || px >= (int)gop_width) continue;
                if (x < r) {
                    int dx = r - x;
                    if (dx * dx + dy2 <= r2) put_pixel((UINTN)px, (UINTN)py, col);
                } else if (x >= w - r) {
                    int dx = x - (w - r - 1);
                    if (dx * dx + dy2 <= r2) put_pixel((UINTN)px, (UINTN)py, col);
                } else {
                    put_pixel((UINTN)px, (UINTN)py, col);
                }
            }
        }
    }
}

void draw_rounded_rect_aa(int sx, int sy, int w, int h, int r, UINT32 color) {
    draw_rounded_rect_gradient(sx, sy, w, h, r, color, color);
}

void wm_draw_text_scaled(const char *str, int x, int y, int scale, UINT32 fg_color, UINT32 bg_color) {
    if (!str || scale <= 0) return;
    int cur_x = x, cur_y = y;

    for (int i = 0; str[i] != '\0'; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c == '\n') { cur_x = x; cur_y += 18 * scale; continue; }

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
                                put_pixel((UINTN)px, (UINTN)py, col);
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
    int max_chars = max_w / 8;
    int cur_y = y, i = 0;

    while (str[i] != '\0' && (cur_y + 16 <= y + max_h)) {
        if (str[i] == '\n') { cur_y += 18; i++; continue; }
        int line_len = 0, last_space = -1;
        while (str[i + line_len] != '\0' && str[i + line_len] != '\n' && line_len < max_chars) {
            if (str[i + line_len] == ' ') last_space = line_len;
            line_len++;
        }
        if (str[i + line_len] == '\0' || str[i + line_len] == '\n') {
            char buf[128]; int c = 0;
            while (c < line_len && c < 127) { buf[c] = str[i + c]; c++; }
            buf[c] = '\0';
            wm_draw_text(buf, x, cur_y, fg_color, bg_color);
            i += line_len;
            if (str[i] == '\n') i++;
            cur_y += 18;
        } else {
            int break_at = (last_space > 0) ? last_space : max_chars;
            if (break_at <= 0) break_at = 1;
            char buf[128]; int c = 0;
            while (c < break_at && c < 127) { buf[c] = str[i + c]; c++; }
            buf[c] = '\0';
            wm_draw_text(buf, x, cur_y, fg_color, bg_color);
            i += break_at;
            if (str[i] == ' ') i++;
            cur_y += 18;
        }
    }
}

// Surface Operations
void wm_surface_clear(int win_id, UINT32 color) {
    Window *win = wm_get_window(win_id);
    if (!win || !win->surface) return;
    int cw = win->is_maximized ? win->width : win->width - 12;
    int ch = win->is_maximized ? win->height - 30 : win->height - 36;
    if (cw > SURFACE_STRIDE) cw = SURFACE_STRIDE;
    if (ch > SURFACE_HEIGHT) ch = SURFACE_HEIGHT;
    if (cw <= 0 || ch <= 0) return;

    for (int y = 0; y < ch; y++) {
        UINT32 *row = &win->surface[y * SURFACE_STRIDE];
        for (int x = 0; x < cw; x++) row[x] = color;
    }
}

void wm_surface_draw_rect(int win_id, int x, int y, int w, int h, UINT32 color) {
    Window *win = wm_get_window(win_id);
    if (!win || !win->surface || w <= 0 || h <= 0) return;
    int max_w = win->is_maximized ? win->width : win->width - 12;
    int max_h = win->is_maximized ? win->height - 30 : win->height - 36;
    if (max_w > SURFACE_STRIDE) max_w = SURFACE_STRIDE;
    if (max_h > SURFACE_HEIGHT) max_h = SURFACE_HEIGHT;

    int x1 = (x < 0) ? 0 : x, y1 = (y < 0) ? 0 : y;
    int x2 = (x + w > max_w) ? max_w : x + w, y2 = (y + h > max_h) ? max_h : y + h;
    if (x1 >= x2 || y1 >= y2) return;

    for (int sy = y1; sy < y2; sy++) {
        UINT32 *row = &win->surface[sy * SURFACE_STRIDE];
        for (int sx = x1; sx < x2; sx++) row[sx] = color;
    }
}

void wm_surface_draw_gradient(int win_id, int x, int y, int w, int h, UINT32 top_col, UINT32 bot_col) {
    Window *win = wm_get_window(win_id);
    if (!win || !win->surface || w <= 0 || h <= 0) return;
    int max_w = win->is_maximized ? win->width : win->width - 12;
    int max_h = win->is_maximized ? win->height - 30 : win->height - 36;
    int x1 = (x < 0) ? 0 : x, y1 = (y < 0) ? 0 : y;
    int x2 = (x + w > max_w) ? max_w : x + w, y2 = (y + h > max_h) ? max_h : y + h;
    if (x1 >= x2 || y1 >= y2) return;

    int tr = (top_col >> 16) & 0xFF, tg = (top_col >> 8) & 0xFF, tb = top_col & 0xFF;
    int br = (bot_col >> 16) & 0xFF, bg_v = (bot_col >> 8) & 0xFF, bb = bot_col & 0xFF;
    int denom = (h > 1 ? (h - 1) : 1);

    for (int sy = y1; sy < y2; sy++) {
        int cy = sy - y;
        int cr = tr + ((br - tr) * cy) / denom;
        int cg = tg + ((bg_v - tg) * cy) / denom;
        int cb = tb + ((bb - tb) * cy) / denom;
        UINT32 col = ((UINT32)cr << 16) | ((UINT32)cg << 8) | (UINT32)cb;

        UINT32 *row = &win->surface[sy * SURFACE_STRIDE];
        for (int sx = x1; sx < x2; sx++) row[sx] = col;
    }
}

void wm_surface_draw_text(int win_id, const char *str, int x, int y, UINT32 color) {
    Window *win = wm_get_window(win_id);
    if (!win || !win->surface || !str) return;
    int max_w = win->is_maximized ? win->width : win->width - 12;
    int max_h = win->is_maximized ? win->height - 30 : win->height - 36;
    if (max_w > SURFACE_STRIDE) max_w = SURFACE_STRIDE;
    if (max_h > SURFACE_HEIGHT) max_h = SURFACE_HEIGHT;

    int cur_x = x, cur_y = y;
    for (int i = 0; str[i] != '\0'; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c == '\n') { cur_x = x; cur_y += 18; continue; }
        if (cur_y + 16 <= max_h && cur_y >= 0) {
            const unsigned char *glyph = font8x16[c];
            for (int gy = 0; gy < 16; gy++) {
                unsigned char row_bits = glyph[gy];
                int py = cur_y + gy;
                if (py < 0 || py >= max_h) continue;
                UINT32 *row = &win->surface[py * SURFACE_STRIDE];
                for (int gx = 0; gx < 8; gx++) {
                    if (row_bits & (1 << (7 - gx))) {
                        int px = cur_x + gx;
                        if (px >= 0 && px < max_w) row[px] = color;
                    }
                }
            }
        }
        cur_x += 8;
    }
}

static Window *g_surf_win = NULL;
static void wm_icon_surface_setter(int px, int py, unsigned int color) {
    if (!g_surf_win || !g_surf_win->surface) return;
    int max_w = g_surf_win->is_maximized ? g_surf_win->width : g_surf_win->width - 12;
    int max_h = g_surf_win->is_maximized ? g_surf_win->height - 30 : g_surf_win->height - 36;
    if (px >= 0 && px < max_w && py >= 0 && py < max_h) {
        g_surf_win->surface[py * SURFACE_STRIDE + px] = (UINT32)color;
    }
}

void wm_surface_draw_icon(int win_id, int icon_type, int x, int y, int size) {
    Window *win = wm_get_window(win_id);
    if (!win || !win->surface) return;
    g_surf_win = win;
    if (icon_type == VELO_ICON_PC) render_icon_pc(x, y, size, wm_icon_surface_setter);
    else if (icon_type == VELO_ICON_FOLDER) render_svg_folder(x, y, size, wm_icon_surface_setter);
    else if (icon_type == VELO_ICON_DOC) render_svg_doc(x, y, size, wm_icon_surface_setter);
    else if (icon_type == VELO_ICON_UNKNOWN) render_svg_unknown(x, y, size, wm_icon_surface_setter);
    else if (icon_type == VELO_ICON_APP) render_svg_app(x, y, size, wm_icon_surface_setter);
    g_surf_win = NULL;
}

void wm_surface_draw_button(int win_id, int x, int y, int w, int h, const char *label) {
    wm_surface_draw_gradient(win_id, x, y, w, h, 0x0038BDF8, 0x000284C7);
    wm_surface_draw_rect(win_id, x, y, w, 1, 0x00BAE6FD);
    wm_surface_draw_rect(win_id, x, y + h - 1, w, 1, 0x00BAE6FD);
    wm_surface_draw_rect(win_id, x, y, 1, h, 0x00BAE6FD);
    wm_surface_draw_rect(win_id, x + w - 1, y, 1, h, 0x00BAE6FD);
    if (label) {
        int l = 0; while (label[l]) l++;
        int tx = x + (w - l * 8) / 2, ty = y + (h - 16) / 2;
        wm_surface_draw_text(win_id, label, tx, ty, 0x00FFFFFF);
    }
}

void wm_surface_draw_storage_bar(int win_id, int x, int y, int w, int percent) {
    wm_surface_draw_gradient(win_id, x, y, w, 14, 0x00E2E8F0, 0x00CBD5E1);
    wm_surface_draw_rect(win_id, x, y, w, 1, 0x007BA3B8);
    wm_surface_draw_rect(win_id, x, y + 13, w, 1, 0x007BA3B8);
    wm_surface_draw_rect(win_id, x, y, 1, 14, 0x007BA3B8);
    wm_surface_draw_rect(win_id, x + w - 1, y, 1, 14, 0x007BA3B8);

    int fill = ((w - 2) * percent) / 100;
    if (fill > 0) {
        wm_surface_draw_gradient(win_id, x + 1, y + 1, fill, 6, 0x007DD3FC, 0x000284C7);
        wm_surface_draw_gradient(win_id, x + 1, y + 7, fill, 6, 0x000369A1, 0x0038BDF8);
        wm_surface_draw_rect(win_id, x + 1, y + 6, fill, 1, 0x00BAE6FD);
    }
}

void wm_surface_draw_sidebar_item(int win_id, int y, int w, const char *label, int is_selected) {
    if (is_selected) {
        wm_surface_draw_gradient(win_id, 6, y - 3, w - 12, 22, 0x00EBF4FB, 0x00D6ECFF);
        wm_surface_draw_rect(win_id, 6, y - 3, w - 12, 1, 0x0060A5FA);
        wm_surface_draw_rect(win_id, 6, y + 18, w - 12, 1, 0x0060A5FA);
        wm_surface_draw_rect(win_id, 6, y - 3, 1, 22, 0x0060A5FA);
        wm_surface_draw_rect(win_id, 6 + w - 13, y - 3, 1, 22, 0x0060A5FA);
        wm_surface_draw_text(win_id, label, 14, y, 0x000F172A);
    } else {
        wm_surface_draw_text(win_id, label, 14, y, 0x001D4ED8);
    }
}

void wm_surface_draw_nav_btn(int win_id, int x, int y, const char *symbol, int enabled) {
    if (!enabled) {
        wm_surface_draw_gradient(win_id, x, y, 26, 26, 0x00E2E8F0, 0x00CBD5E1);
        wm_surface_draw_rect(win_id, x, y, 26, 1, 0x00CBD5E1);
        wm_surface_draw_text(win_id, symbol, x + 9, y + 5, 0x0094A3B8);
    } else {
        wm_surface_draw_gradient(win_id, x, y, 26, 26, 0x000284C7, 0x000F4866);
        wm_surface_draw_rect(win_id, x, y, 26, 1, 0x0038BDF8);
        wm_surface_draw_text(win_id, symbol, x + 9, y + 5, 0x00FFFFFF);
    }
}

void wm_surface_draw_addressbar(int win_id, int x, int y, int w, const char *path) {
    wm_surface_draw_gradient(win_id, x, y, w, 26, 0x00FFFFFF, 0x00F8FAFC);
    wm_surface_draw_rect(win_id, x, y, w, 1, 0x007BA3B8);
    wm_surface_draw_rect(win_id, x, y + 25, w, 1, 0x0094A3B8);
    wm_surface_draw_rect(win_id, x, y, 1, 26, 0x007BA3B8);
    wm_surface_draw_rect(win_id, x + w - 1, y, 1, 26, 0x007BA3B8);
    wm_surface_draw_text(win_id, "[=]", x + 8, y + 5, 0x000284C7);
    if (path) wm_surface_draw_text(win_id, path, x + 38, y + 5, 0x000F172A);
}

void wm_surface_draw_searchbox(int win_id, int x, int y, int w, const char *query, int cursor_pos, int focused) {
    UINT32 border = focused ? 0x000284C7 : 0x007BA3B8;
    wm_surface_draw_gradient(win_id, x, y, w, 26, 0x00FFFFFF, 0x00F8FAFC);
    wm_surface_draw_rect(win_id, x, y, w, 1, border);
    wm_surface_draw_rect(win_id, x, y + 25, w, 1, border);
    wm_surface_draw_rect(win_id, x, y, 1, 26, border);
    wm_surface_draw_rect(win_id, x + w - 1, y, 1, 26, border);

    if (query && query[0]) {
        wm_surface_draw_text(win_id, query, x + 8, y + 5, 0x000F172A);
        if (focused) wm_surface_draw_rect(win_id, x + 8 + cursor_pos * 8, y + 4, 1, 16, 0x000284C7);
    } else {
        wm_surface_draw_text(win_id, "Search...", x + 8, y + 5, 0x0094A3B8);
        if (focused) wm_surface_draw_rect(win_id, x + 8 + cursor_pos * 8, y + 4, 1, 16, 0x000284C7);
    }
}

void wm_surface_draw_command_bar(int win_id, int y, int w) {
    wm_surface_draw_gradient(win_id, 0, y, w, 28, 0x001B4D68, 0x000A2434);
    wm_surface_draw_rect(win_id, 0, y, w, 1, 0x003A7088);
    wm_surface_draw_rect(win_id, 0, y + 27, w, 1, 0x0005141C);

    wm_surface_draw_text(win_id, "+Folder", 10, y + 6, 0x004ADE80);
    wm_surface_draw_rect(win_id, 70, y + 4, 1, 20, 0x002B5268);
    wm_surface_draw_text(win_id, "+File", 78, y + 6, 0x004ADE80);
    wm_surface_draw_rect(win_id, 126, y + 4, 1, 20, 0x002B5268);
    wm_surface_draw_text(win_id, "Copy", 134, y + 6, 0x00FFFFFF);
    wm_surface_draw_rect(win_id, 172, y + 4, 1, 20, 0x002B5268);
    wm_surface_draw_text(win_id, "Cut", 180, y + 6, 0x00FFFFFF);
    wm_surface_draw_rect(win_id, 210, y + 4, 1, 20, 0x002B5268);
    wm_surface_draw_text(win_id, "Paste", 218, y + 6, 0x00FFFFFF);
    wm_surface_draw_rect(win_id, 264, y + 4, 1, 20, 0x002B5268);
    wm_surface_draw_text(win_id, "Rename", 272, y + 6, 0x0038BDF8);
    wm_surface_draw_rect(win_id, 326, y + 4, 1, 20, 0x002B5268);
    wm_surface_draw_text(win_id, "Delete", 334, y + 6, 0x00F87171);
}

void wm_surface_draw_modal_dialog(int win_id, int x, int y, int w, int h, const char *title) {
    wm_surface_draw_rect(win_id, x - 3, y - 3, w + 6, h + 6, 0x0064748B);
    wm_surface_draw_gradient(win_id, x, y, w, h, 0x00FFFFFF, 0x00F8FAFC);
    wm_surface_draw_rect(win_id, x, y, w, 1, 0x000284C7);
    wm_surface_draw_rect(win_id, x, y + h - 1, w, 1, 0x0094A3B8);
    wm_surface_draw_gradient(win_id, x, y, w, 32, 0x001B4D68, 0x000A2434);
    if (title) wm_surface_draw_text(win_id, title, x + 14, y + 8, 0x00FFFFFF);
}

void wm_init(void) {
    g_window_count = 0;
    g_z_count = 0;
    g_active_win_id = -1;
    g_drag_win_id = -1;

    UINTN surf_bytes = SURFACE_STRIDE * SURFACE_HEIGHT * sizeof(UINT32);

    for (int i = 0; i < MAX_WINDOWS; i++) {
        g_windows[i].is_closed = 1;
        g_windows[i].on_click = NULL;
        g_windows[i].on_key = NULL;
        g_windows[i].key_q_head = 0;
        g_windows[i].key_q_tail = 0;

        if (!g_win_surfaces[i]) {
            UINTN pages = EFI_SIZE_TO_PAGES(surf_bytes);
            EFI_PHYSICAL_ADDRESS phys = 0;
            if (BS && BS->AllocatePages) {
                EFI_STATUS status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, pages, &phys);
                if (status == EFI_SUCCESS && phys != 0) g_win_surfaces[i] = (UINT32*)(UINTN)phys;
            }
            if (!g_win_surfaces[i]) {
                g_win_surfaces[i] = (UINT32*)AllocateZeroPool(surf_bytes);
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
    win->x = x; win->y = y; win->width = w; win->height = h;
    win->orig_x = x; win->orig_y = y; win->orig_w = w; win->orig_h = h;
    win->is_active = 1; win->is_minimized = 0; win->is_maximized = 0; win->is_closed = 0;
    win->is_dirty_content = 1; win->bg_color = 0x00F8FAFC;
    win->surface = g_win_surfaces[slot];
    win->key_q_head = 0; win->key_q_tail = 0;
    win->on_paint = on_paint; win->on_click = NULL; win->on_key = NULL;

    int cw = w - 12, ch = h - 36;
    if (cw > SURFACE_STRIDE) cw = SURFACE_STRIDE;
    if (ch > SURFACE_HEIGHT) ch = SURFACE_HEIGHT;
    if (win->surface && cw > 0 && ch > 0) {
        for (int py = 0; py < ch; py++) {
            for (int px = 0; px < cw; px++) win->surface[py * SURFACE_STRIDE + px] = win->bg_color;
        }
    }

    int p = 0;
    while (title[p] && p < 63) { win->title[p] = title[p]; p++; }
    win->title[p] = '\0';

    g_window_count++;
    wm_focus_window(slot);
    wm_mark_all_dirty();
    return slot;
}

int wm_create_window_auto(const char *title, int content_w, int content_h, void (*on_paint)(int, int, int, int, int)) {
    int max_avail_w = (int)gop_width - 40, max_avail_h = (int)gop_height - 60;
    if (content_w > max_avail_w) content_w = max_avail_w;
    if (content_h > max_avail_h) content_h = max_avail_h;

    int total_w = content_w + 12;
    if (total_w < 260) total_w = 260;
    int total_h = content_h + 36;
    int x = ((int)gop_width - total_w) / 2, y = ((int)gop_height - 40 - total_h) / 2;
    if (x < 10) x = 10;
    if (y < 20) y = 20;

    return wm_create_window(x, y, total_w, total_h, title, on_paint);
}

void wm_close_window(int id) {
    if (id < 0 || id >= MAX_WINDOWS || g_windows[id].is_closed) return;
    g_windows[id].is_closed = 1;
    g_windows[id].is_active = 0;
    g_window_count--;

    for (int i = 0; i < g_z_count; i++) {
        if (g_z_order[i] == id) {
            for (int j = i; j < g_z_count - 1; j++) g_z_order[j] = g_z_order[j + 1];
            g_z_count--;
            break;
        }
    }

    if (g_drag_win_id == id) g_drag_win_id = -1;
    if (g_active_win_id == id) {
        g_active_win_id = (g_z_count > 0) ? g_z_order[g_z_count - 1] : -1;
        if (g_active_win_id >= 0) g_windows[g_active_win_id].is_active = 1;
    }
    wm_mark_all_dirty();
}

void wm_close_active(void) {
    if (g_active_win_id >= 0) wm_close_window(g_active_win_id);
}

void wm_minimize_window(int id) {
    if (id < 0 || id >= MAX_WINDOWS || g_windows[id].is_closed) return;
    Window *win = &g_windows[id];
    win->is_minimized = !win->is_minimized;

    if (win->is_minimized) {
        win->is_active = 0;
        if (g_active_win_id == id) {
            g_active_win_id = -1;
            for (int i = g_z_count - 1; i >= 0; i--) {
                int zid = g_z_order[i];
                if (!g_windows[zid].is_closed && !g_windows[zid].is_minimized) {
                    wm_focus_window(zid);
                    break;
                }
            }
        }
    } else {
        wm_focus_window(id);
    }
    wm_mark_all_dirty();
}

void wm_minimize_active(void) {
    if (g_active_win_id >= 0) wm_minimize_window(g_active_win_id);
}

void wm_minimize_all(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!g_windows[i].is_closed && !g_windows[i].is_minimized) {
            g_windows[i].is_minimized = 1;
            g_windows[i].is_active = 0;
        }
    }
    g_active_win_id = -1;
    wm_mark_all_dirty();
}

void wm_maximize_window(int id) {
    if (id < 0 || id >= MAX_WINDOWS || g_windows[id].is_closed) return;
    Window *win = &g_windows[id];
    int new_w, new_h;

    if (!win->is_maximized) {
        win->orig_x = win->x; win->orig_y = win->y;
        win->orig_w = win->width; win->orig_h = win->height;
        win->x = 0; win->y = 0;
        new_w = (int)gop_width; new_h = (int)gop_height - 38;
        win->is_maximized = 1;
    } else {
        win->x = win->orig_x; win->y = win->orig_y;
        new_w = win->orig_w; new_h = win->orig_h;
        win->is_maximized = 0;
    }

    win->width = new_w; win->height = new_h;
    win->is_dirty_content = 1;
    wm_surface_clear(id, win->bg_color);

    if (win->on_paint) {
        int off_x = win->is_maximized ? 0 : 6, off_y = 30;
        int cw = win->is_maximized ? win->width : win->width - 12;
        int ch = win->is_maximized ? win->height - 30 : win->height - 36;
        win->on_paint(win->id, win->x + off_x, win->y + off_y, cw, ch);
    }

    wm_focus_window(id);
    wm_mark_all_dirty();
}

void wm_maximize_active(void) {
    if (g_active_win_id >= 0) wm_maximize_window(g_active_win_id);
}

void wm_focus_window(int id) {
    if (id < 0 || id >= MAX_WINDOWS || g_windows[id].is_closed) return;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        g_windows[i].is_active = (i == id);
    }
    g_windows[id].is_minimized = 0;
    g_active_win_id = id;

    int cur_pos = -1;
    for (int i = 0; i < g_z_count; i++) {
        if (g_z_order[i] == id) { cur_pos = i; break; }
    }
    if (cur_pos != -1) {
        for (int i = cur_pos; i < g_z_count - 1; i++) g_z_order[i] = g_z_order[i + 1];
        g_z_order[g_z_count - 1] = id;
    } else {
        g_z_order[g_z_count++] = id;
    }
    wm_mark_all_dirty();
}

void wm_focus_next(void) {
    if (g_z_count <= 1) return;
    int first = g_z_order[0];
    for (int i = 0; i < g_z_count - 1; i++) g_z_order[i] = g_z_order[i + 1];
    g_z_order[g_z_count - 1] = first;
    wm_focus_window(g_z_order[g_z_count - 1]);
}

Window* wm_get_window(int id) {
    if (id < 0 || id >= MAX_WINDOWS || g_windows[id].is_closed) return NULL;
    return &g_windows[id];
}

int wm_get_window_count(void) { return g_window_count; }
int wm_get_active_window_id(void) { return g_active_win_id; }

static void render_window_frame(Window *win) {
    if (win->is_closed || win->is_minimized) return;
    int x = win->x, y = win->y, w = win->width, h = win->height;

    // Weicher Gaussian Drop Shadow
    if (!win->is_maximized) {
        draw_shadow_box(x, y, w, h, 6, 8);
        UINT32 aero_border = win->is_active ? 0x004A8FA8 : 0x002A3D48;
        draw_rounded_rect_aa(x, y, w, h, 6, aero_border);
    }

    // Aero Glass Titelleiste mit Lichtkante
    UINT32 top_glass = win->is_active ? 0x001B485A : 0x0012242D;
    UINT32 bot_glass = win->is_active ? 0x000B2430 : 0x0008141A;
    int title_r = win->is_maximized ? 0 : 5;
    draw_rounded_rect_gradient(x, y, w, 30, title_r, top_glass, bot_glass);
    draw_filled_rect((UINTN)x, (UINTN)(y + 15), (UINTN)w, 1, win->is_active ? 0x003A7088 : 0x00223D48);

    wm_draw_text_shadow(win->title, x + 10, y + 7, 1, 0x00FFFFFF, 0x0002060C);

    // Fensterknöpfe
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

    // Inhalt per Clipping-Blit übertragen
    int off_x = win->is_maximized ? 0 : 6, off_y = 30;
    int cw = win->is_maximized ? win->width : win->width - 12;
    int ch = win->is_maximized ? win->height - 30 : win->height - 36;

    if (win->surface && cw > 0 && ch > 0 && g_backbuffer) {
        if (cw > SURFACE_STRIDE) cw = SURFACE_STRIDE;
        if (ch > SURFACE_HEIGHT) ch = SURFACE_HEIGHT;
        
        int dst_x = x + off_x, dst_y = y + off_y;
        int src_x = 0, draw_w = cw;

        if (dst_x < 0) { src_x = -dst_x; draw_w += dst_x; dst_x = 0; }
        if (dst_x + draw_w > (int)gop_width) draw_w = (int)gop_width - dst_x;

        if (draw_w > 0) {
            UINTN copy_bytes = (UINTN)draw_w * sizeof(UINT32);
            for (int cy = 0; cy < ch; cy++) {
                int dy = dst_y + cy;
                if (dy < 0 || dy >= (int)gop_height) continue;
                UINT32 *dst_row = &g_backbuffer[dy * gop_width + dst_x];
                const UINT32 *src_row = &win->surface[cy * SURFACE_STRIDE + src_x];
                __builtin_memcpy(dst_row, src_row, copy_bytes);
            }
        }
    }

    if (win->on_paint) {
        win->on_paint(win->id, x + off_x, y + off_y, cw, ch);
    }
}

void wm_render_all(void) {
    for (int i = 0; i < g_z_count; i++) {
        int id = g_z_order[i];
        if (id >= 0 && id < MAX_WINDOWS && !g_windows[id].is_closed && !g_windows[id].is_minimized) {
            render_window_frame(&g_windows[id]);
        }
    }
}
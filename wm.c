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

static unsigned int isqrt(unsigned int val) {
    unsigned int temp, g = 0;
    unsigned int b = 0x8000;
    while (b) {
        temp = g + b;
        if (temp * temp <= val) {
            g = temp;
        }
        b >>= 1;
    }
    return g;
}

static inline void update_content_bounds(Window *win, int right, int bottom) {
    if (right > win->content_w) win->content_w = right;
    if (bottom > win->content_h) win->content_h = bottom;
}

static inline int kstrlen(const char *s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

UINT32 wm_get_contrast_color(UINT32 bg_color) {
    unsigned int r = (bg_color >> 16) & 0xFF;
    unsigned int g = (bg_color >> 8) & 0xFF;
    unsigned int b = bg_color & 0xFF;
    unsigned int luminance = (r * 2126 + g * 7152 + b * 722) / 10000;
    return (luminance >= 135) ? 0x000F172A : 0x00F8FAFC;
}

void wm_mark_dirty(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    int x2 = x + w, y2 = y + h;
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

void wm_window_set_auto_scroll(int win_id, int enabled) {
    Window *win = wm_get_window(win_id);
    if (win) win->auto_scroll = enabled;
}

void wm_window_scroll(int win_id, int dx, int dy) {
    Window *win = wm_get_window(win_id);
    if (!win || win->is_closed) return;
    int ch = win->is_maximized ? win->height - 30 : win->height - 36;
    int cw = win->is_maximized ? win->width : win->width - 12;

    win->scroll_y += dy;
    win->scroll_x += dx;

    int max_sy = win->content_h - ch;
    if (max_sy < 0) max_sy = 0;
    if (win->scroll_y < 0) win->scroll_y = 0;
    if (win->scroll_y > max_sy) win->scroll_y = max_sy;

    int max_sx = win->content_w - cw;
    if (max_sx < 0) max_sx = 0;
    if (win->scroll_x < 0) win->scroll_x = 0;
    if (win->scroll_x > max_sx) win->scroll_x = max_sx;

    wm_mark_dirty(win->x, win->y, win->width, win->height);
}

void wm_window_push_event(int win_id, int type, int x, int y, char key, int sx, int sy) {
    if (win_id < 0 || win_id >= MAX_WINDOWS || g_windows[win_id].is_closed) return;
    Window *win = &g_windows[win_id];
    int next = (win->ev_q_head + 1) % WIN_EVENT_QUEUE_SIZE;
    if (next != win->ev_q_tail) {
        win->ev_queue[win->ev_q_head].type = type;
        win->ev_queue[win->ev_q_head].x = x;
        win->ev_queue[win->ev_q_head].y = y;
        win->ev_queue[win->ev_q_head].key = key;
        win->ev_queue[win->ev_q_head].scroll_x = sx;
        win->ev_queue[win->ev_q_head].scroll_y = sy;
        win->ev_q_head = next;
    }
}

int wm_window_pop_event(int win_id, WinEvent *out_ev) {
    if (win_id < 0 || win_id >= MAX_WINDOWS || g_windows[win_id].is_closed || !out_ev) return 0;
    Window *win = &g_windows[win_id];
    if (win->ev_q_head == win->ev_q_tail) return 0;
    *out_ev = win->ev_queue[win->ev_q_tail];
    win->ev_q_tail = (win->ev_q_tail + 1) % WIN_EVENT_QUEUE_SIZE;
    return 1;
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

    if (new_y < 24) new_y = 24;
    if (new_y > (int)gop_height - 60) new_y = (int)gop_height - 60;

    int max_x = (int)gop_width - win->width;
    if (max_x < 0) max_x = 0;
    if (new_x > max_x) new_x = max_x;
    if (new_x < 0) new_x = 0;

    if (new_x != old_x || new_y != old_y) {
        win->x = new_x;
        win->y = new_y;
        wm_mark_dirty(old_x - 8, old_y - 8, win->width + 20, win->height + 20);
        wm_mark_dirty(new_x - 8, new_y - 8, win->width + 20, win->height + 20);
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
    UINT32 rb = (((fg & 0x00FF00FF) * alpha + (bg & 0x00FF00FF) * inv) >> 8) & 0x00FF00FF;
    UINT32 g  = (((fg & 0x0000FF00) * alpha + (bg & 0x0000FF00) * inv) >> 8) & 0x0000FF00;
    return rb | g;
}

static void draw_shadow_box(int sx, int sy, int w, int h, int radius) {
    (void)radius;
    int ox = sx - 6, oy = sy - 3;
    int ow = w + 12, oh = h + 12;

    for (int y = 0; y < oh; y++) {
        int py = oy + y;
        if (py < 0 || py >= (int)gop_height) continue;
        for (int x = 0; x < ow; x++) {
            int px = ox + x;
            if (px < 0 || px >= (int)gop_width) continue;
            if (px >= sx && px < sx + w && py >= sy && py < sy + h) continue;

            UINT32 bg = get_pixel((UINTN)px, (UINTN)py);
            put_pixel((UINTN)px, (UINTN)py, alpha_blend(0x00000000, bg, 55));
        }
    }
}

void draw_circle_aa(int cx, int cy, int r, UINT32 color) {
    if (r <= 0) return;
    int min_x = cx - r - 1;
    int max_x = cx + r + 1;
    int min_y = cy - r - 1;
    int max_y = cy + r + 1;

    int r_inner_fp = (r * 256) - 128;
    int r_outer_fp = (r * 256) + 128;

    for (int y = min_y; y <= max_y; y++) {
        if (y < 0 || y >= (int)gop_height) continue;
        int dy = y - cy;
        int dy2 = dy * dy;

        for (int x = min_x; x <= max_x; x++) {
            if (x < 0 || x >= (int)gop_width) continue;
            int dx = x - cx;
            int dist_sq = (dx * dx + dy2) * 65536;
            int dist_fp = (int)isqrt((unsigned int)dist_sq);

            if (dist_fp <= r_inner_fp) {
                put_pixel((UINTN)x, (UINTN)y, color);
            } else if (dist_fp < r_outer_fp) {
                int cov = (r_outer_fp - dist_fp);
                if (cov < 0) cov = 0;
                if (cov > 255) cov = 255;
                UINT32 bg = get_pixel((UINTN)x, (UINTN)y);
                put_pixel((UINTN)x, (UINTN)y, alpha_blend(color, bg, (UINT32)cov));
            }
        }
    }
}

void draw_circle_button_aa(int cx, int cy, int r, UINT32 fill_color, UINT32 border_color) {
    draw_circle_aa(cx, cy, r, border_color);
    if (r > 1) {
        draw_circle_aa(cx, cy, r - 1, fill_color);
    }
}

void draw_rounded_rect_gradient(int sx, int sy, int w, int h, int r, UINT32 top_col, UINT32 bot_col) {
    if (w <= 0 || h <= 0) return;
    int tr = (top_col >> 16) & 0xFF, tg = (top_col >> 8) & 0xFF, tb = top_col & 0xFF;
    int br = (bot_col >> 16) & 0xFF, bg_v = (bot_col >> 8) & 0xFF, bb = bot_col & 0xFF;
    int denom = (h > 1 ? (h - 1) : 1);

    int r_inner_fp = (r * 256) - 128;
    int r_outer_fp = (r * 256) + 128;

    for (int y = 0; y < h; y++) {
        int py = sy + y;
        if (py < 0 || py >= (int)gop_height) continue;

        int cr = tr + ((br - tr) * y) / denom;
        int cg = tg + ((bg_v - tg) * y) / denom;
        int cb = tb + ((bb - tb) * y) / denom;
        UINT32 col = ((UINT32)cr << 16) | ((UINT32)cg << 8) | (UINT32)cb;

        int is_corner_y = (r > 0) && ((y < r) || (y >= h - r));
        int dy = (y < r) ? (r - 1 - y) : ((y >= h - r) ? (y - (h - r)) : 0);
        int dy2 = dy * dy;

        for (int x = 0; x < w; x++) {
            int px = sx + x;
            if (px < 0 || px >= (int)gop_width) continue;

            int is_corner_x = (r > 0) && ((x < r) || (x >= w - r));

            if (!is_corner_x || !is_corner_y) {
                put_pixel((UINTN)px, (UINTN)py, col);
            } else {
                int dx = (x < r) ? (r - 1 - x) : (x - (w - r));
                int dist_sq = (dx * dx + dy2) * 65536;
                int dist_fp = (int)isqrt((unsigned int)dist_sq);

                if (dist_fp <= r_inner_fp) {
                    put_pixel((UINTN)px, (UINTN)py, col);
                } else if (dist_fp < r_outer_fp) {
                    int cov = (r_outer_fp - dist_fp);
                    if (cov < 0) cov = 0;
                    if (cov > 255) cov = 255;
                    UINT32 bg = get_pixel((UINTN)px, (UINTN)py);
                    put_pixel((UINTN)px, (UINTN)py, alpha_blend(col, bg, (UINT32)cov));
                }
            }
        }
    }
}

void draw_rounded_rect_aa(int sx, int sy, int w, int h, int r, UINT32 color) {
    draw_rounded_rect_gradient(sx, sy, w, h, r, color, color);
}

void draw_frosted_glass_rect(int sx, int sy, int w, int h, int r, UINT32 tint_col, UINT32 alpha) {
    if (w <= 0 || h <= 0) return;
    int r_inner_fp = (r * 256) - 128;
    int r_outer_fp = (r * 256) + 128;

    for (int y = 0; y < h; y++) {
        int py = sy + y;
        if (py < 0 || py >= (int)gop_height) continue;

        int is_corner_y = (r > 0) && ((y < r) || (y >= h - r));
        int dy = (y < r) ? (r - 1 - y) : ((y >= h - r) ? (y - (h - r)) : 0);
        int dy2 = dy * dy;

        for (int x = 0; x < w; x++) {
            int px = sx + x;
            if (px < 0 || px >= (int)gop_width) continue;

            int is_corner_x = (r > 0) && ((x < r) || (x >= w - r));
            int cov_alpha = alpha;

            if (is_corner_x && is_corner_y) {
                int dx = (x < r) ? (r - 1 - x) : (x - (w - r));
                int dist_sq = (dx * dx + dy2) * 65536;
                int dist_fp = (int)isqrt((unsigned int)dist_sq);

                if (dist_fp >= r_outer_fp) continue;
                if (dist_fp > r_inner_fp) {
                    int cov = (r_outer_fp - dist_fp);
                    cov_alpha = (alpha * cov) / 256;
                }
            }

            UINT32 sum_r = 0, sum_g = 0, sum_b = 0;
            int samples = 0;

            for (int ky = -1; ky <= 1; ky++) {
                int by = py + ky;
                if (by < 0 || by >= (int)gop_height) continue;
                for (int kx = -1; kx <= 1; kx++) {
                    int bx = px + kx;
                    if (bx < 0 || bx >= (int)gop_width) continue;
                    UINT32 pcol = get_pixel((UINTN)bx, (UINTN)by);
                    sum_r += (pcol >> 16) & 0xFF;
                    sum_g += (pcol >> 8) & 0xFF;
                    sum_b += pcol & 0xFF;
                    samples++;
                }
            }

            UINT32 blurred_bg = (samples > 0) ? 
                (((sum_r / samples) << 16) | ((sum_g / samples) << 8) | (sum_b / samples)) : 
                get_pixel((UINTN)px, (UINTN)py);

            UINT32 glass_pixel = alpha_blend(tint_col, blurred_bg, (UINT32)cov_alpha);
            put_pixel((UINTN)px, (UINTN)py, glass_pixel);
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

void wm_draw_text_auto(const char *str, int x, int y, UINT32 bg_color) {
    UINT32 contrast_fg = wm_get_contrast_color(bg_color);
    wm_draw_text(str, x, y, contrast_fg, 0x00000000);
}

void wm_draw_text_scaled(const char *str, int x, int y, int scale, UINT32 fg_color, UINT32 bg_color) {
    if (!str || scale <= 0) return;
    int cur_x = x, cur_y = y;

    for (int i = 0; str[i] != '\0'; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c == '\n') { cur_x = x; cur_y += 18 * scale; continue; }

        if (cur_x >= (int)gop_width) break;

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

void wm_surface_draw_text(int win_id, const char *str, int x, int y, UINT32 color) {
    Window *win = wm_get_window(win_id);
    if (!win || !win->surface || !str) return;

    int max_w = SURFACE_STRIDE;
    int max_h = SURFACE_HEIGHT;
    int min_y = (win->scroll_y > 0) ? win->scroll_y : 0;
    int min_x = (win->scroll_x > 0) ? win->scroll_x : 0;
    int cur_x = x, cur_y = y;

    for (int i = 0; str[i] != '\0'; ) {
        unsigned char c = (unsigned char)str[i];

        if (c == '\n') {
            cur_x = x;
            cur_y += 18;
            i++;
            continue;
        }

        if (c != ' ') {
            int word_len = 0;
            while (str[i + word_len] && str[i + word_len] != ' ' && str[i + word_len] != '\n') {
                word_len++;
            }
            int word_px = word_len * 8;
            if (cur_x > x && (cur_x + word_px > max_w) && (x + word_px <= max_w)) {
                cur_x = x;
                cur_y += 18;
            }
        }

        if (cur_x + 8 > max_w) {
            cur_x = x;
            cur_y += 18;
            if (c == ' ') { i++; continue; }
        }

        if (cur_y >= max_h) break;

        int y1 = (cur_y < min_y) ? min_y : cur_y;
        int y2 = (cur_y + 16 > max_h) ? max_h : (cur_y + 16);

        if (y1 < y2) {
            int x1 = (cur_x < min_x) ? min_x : cur_x;
            int x2 = (cur_x + 8 > max_w) ? max_w : (cur_x + 8);

            if (x1 < x2) {
                const unsigned char *glyph = font8x16[c];
                for (int py = y1; py < y2; py++) {
                    int gy = py - cur_y;
                    unsigned char row_bits = glyph[gy];
                    UINT32 *row = &win->surface[py * SURFACE_STRIDE];
                    for (int px = x1; px < x2; px++) {
                        int gx = px - cur_x;
                        if (row_bits & (1 << (7 - gx))) {
                            row[px] = color;
                        }
                    }
                }
            }
        }

        cur_x += 8;
        update_content_bounds(win, cur_x, cur_y + 18);
        i++;
    }
}

void wm_surface_clear(int win_id, UINT32 color) {
    Window *win = wm_get_window(win_id);
    if (!win || !win->surface) return;

    int cw = win->is_maximized ? win->width : win->width - 12;
    int ch = win->is_maximized ? win->height - 30 : win->height - 36;
    if (cw > SURFACE_STRIDE) cw = SURFACE_STRIDE;

    int clear_h = win->content_h > ch ? win->content_h : ch;
    int needed_h = win->scroll_y + ch;
    if (needed_h > clear_h) clear_h = needed_h;
    if (clear_h > SURFACE_HEIGHT) clear_h = SURFACE_HEIGHT;
    if (cw <= 0 || clear_h <= 0) return;

    for (int y = 0; y < clear_h; y++) {
        UINT32 *row = &win->surface[y * SURFACE_STRIDE];
        for (int x = 0; x < cw; x++) row[x] = color;
    }

    win->content_w = cw;
    win->content_h = ch;
}

void wm_surface_draw_rect(int win_id, int x, int y, int w, int h, UINT32 color) {
    Window *win = wm_get_window(win_id);
    if (!win || !win->surface || w <= 0 || h <= 0) return;

    int max_w = SURFACE_STRIDE;
    int max_h = SURFACE_HEIGHT;
    int min_y = (win->scroll_y > 0) ? min_y = win->scroll_y : 0;
    int min_x = (win->scroll_x > 0) ? min_x = win->scroll_x : 0;

    int x1 = (x < min_x) ? min_x : x;
    int y1 = (y < min_y) ? min_y : y;
    int x2 = (x + w > max_w) ? max_w : x + w;
    int y2 = (y + h > max_h) ? max_h : y + h;
    if (x1 >= x2 || y1 >= y2) return;

    for (int sy = y1; sy < y2; sy++) {
        UINT32 *row = &win->surface[sy * SURFACE_STRIDE];
        for (int sx = x1; sx < x2; sx++) row[sx] = color;
    }
    update_content_bounds(win, x + w, y + h);
}

void wm_surface_draw_gradient(int win_id, int x, int y, int w, int h, UINT32 top_col, UINT32 bot_col) {
    Window *win = wm_get_window(win_id);
    if (!win || !win->surface || w <= 0 || h <= 0) return;

    int max_w = SURFACE_STRIDE;
    int max_h = SURFACE_HEIGHT;
    int min_y = (win->scroll_y > 0) ? win->scroll_y : 0;
    int min_x = (win->scroll_x > 0) ? win->scroll_x : 0;

    int x1 = (x < min_x) ? min_x : x;
    int y1 = (y < min_y) ? min_y : y;
    int x2 = (x + w > max_w) ? max_w : x + w;
    int y2 = (y + h > max_h) ? max_h : y + h;
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
    update_content_bounds(win, x + w, y + h);
}

static Window *g_surf_win = NULL;
static void wm_icon_surface_setter(int px, int py, unsigned int color) {
    if (!g_surf_win || !g_surf_win->surface) return;
    int min_y = (g_surf_win->scroll_y > 0) ? g_surf_win->scroll_y : 0;
    int min_x = (g_surf_win->scroll_x > 0) ? g_surf_win->scroll_x : 0;
    if (px >= min_x && px < SURFACE_STRIDE && py >= min_y && py < SURFACE_HEIGHT) {
        g_surf_win->surface[py * SURFACE_STRIDE + px] = (UINT32)color;
        update_content_bounds(g_surf_win, px + 1, py + 1);
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
    UINT32 bg_top = 0x000284C7;
    UINT32 bg_bot = 0x000369A1;
    wm_surface_draw_gradient(win_id, x, y, w, h, bg_top, bg_bot);
    wm_surface_draw_rect(win_id, x, y, w, 1, 0x0038BDF8);
    wm_surface_draw_rect(win_id, x, y + h - 1, w, 1, 0x0038BDF8);
    wm_surface_draw_rect(win_id, x, y, 1, h, 0x0038BDF8);
    wm_surface_draw_rect(win_id, x + w - 1, y, 1, h, 0x0038BDF8);
    if (label) {
        int l = 0; while (label[l]) l++;
        int tx = x + (w - l * 8) / 2, ty = y + (h - 16) / 2;
        UINT32 contrast_text = wm_get_contrast_color(bg_top);
        wm_surface_draw_text(win_id, label, tx, ty, contrast_text);
    }
}

void wm_surface_draw_storage_bar(int win_id, int x, int y, int w, int percent) {
    wm_surface_draw_gradient(win_id, x, y, w, 14, 0x001E293B, 0x000F172A);
    wm_surface_draw_rect(win_id, x, y, w, 1, 0x00334155);
    wm_surface_draw_rect(win_id, x, y + 13, w, 1, 0x00334155);
    wm_surface_draw_rect(win_id, x, y, 1, 14, 0x00334155);
    wm_surface_draw_rect(win_id, x + w - 1, y, 1, 14, 0x00334155);

    int fill = ((w - 2) * percent) / 100;
    if (fill > 0) {
        wm_surface_draw_gradient(win_id, x + 1, y + 1, fill, 6, 0x0038BDF8, 0x000284C7);
        wm_surface_draw_gradient(win_id, x + 1, y + 7, fill, 6, 0x000369A1, 0x000284C7);
        wm_surface_draw_rect(win_id, x + 1, y + 6, fill, 1, 0x007DD3FC);
    }
}

void wm_surface_draw_sidebar_item(int win_id, int y, int w, const char *label, int is_selected) {
    if (is_selected) {
        wm_surface_draw_gradient(win_id, 6, y - 3, w - 12, 22, 0x000284C7, 0x000369A1);
        wm_surface_draw_rect(win_id, 6, y - 3, w - 12, 1, 0x0038BDF8);
        wm_surface_draw_rect(win_id, 6, y + 18, w - 12, 1, 0x0038BDF8);
        wm_surface_draw_rect(win_id, 6, y - 3, 1, 22, 0x0038BDF8);
        wm_surface_draw_rect(win_id, 6 + w - 13, y - 3, 1, 22, 0x0038BDF8);
        wm_surface_draw_text(win_id, label, 14, y, 0x00FFFFFF);
    } else {
        wm_surface_draw_text(win_id, label, 14, y, 0x0094A3B8);
    }
}

void wm_surface_draw_nav_btn(int win_id, int x, int y, const char *symbol, int enabled) {
    if (!enabled) {
        wm_surface_draw_gradient(win_id, x, y, 26, 26, 0x001E293B, 0x000F172A);
        wm_surface_draw_rect(win_id, x, y, 26, 1, 0x00334155);
        wm_surface_draw_text(win_id, symbol, x + 9, y + 5, 0x0064748B);
    } else {
        wm_surface_draw_gradient(win_id, x, y, 26, 26, 0x000284C7, 0x000F4866);
        wm_surface_draw_rect(win_id, x, y, 26, 1, 0x0038BDF8);
        wm_surface_draw_text(win_id, symbol, x + 9, y + 5, 0x00FFFFFF);
    }
}

void wm_surface_draw_addressbar(int win_id, int x, int y, int w, const char *path) {
    wm_surface_draw_gradient(win_id, x, y, w, 26, 0x000F172A, 0x001E293B);
    wm_surface_draw_rect(win_id, x, y, w, 1, 0x0038BDF8);
    wm_surface_draw_rect(win_id, x, y + 25, w, 1, 0x0038BDF8);
    wm_surface_draw_rect(win_id, x, y, 1, 26, 0x0038BDF8);
    wm_surface_draw_rect(win_id, x + w - 1, y, 1, 26, 0x0038BDF8);
    wm_surface_draw_text(win_id, "[=]", x + 8, y + 5, 0x0038BDF8);
    if (path) wm_surface_draw_text(win_id, path, x + 38, y + 5, 0x00FFFFFF);
}

void wm_surface_draw_searchbox(int win_id, int x, int y, int w, const char *query, int cursor_pos, int focused) {
    UINT32 border = focused ? 0x0038BDF8 : 0x00334155;
    wm_surface_draw_gradient(win_id, x, y, w, 26, 0x000F172A, 0x001E293B);
    wm_surface_draw_rect(win_id, x, y, w, 1, border);
    wm_surface_draw_rect(win_id, x, y + 25, w, 1, border);
    wm_surface_draw_rect(win_id, x, y, 1, 26, border);
    wm_surface_draw_rect(win_id, x + w - 1, y, 1, 26, border);

    if (query && query[0]) {
        wm_surface_draw_text(win_id, query, x + 8, y + 5, 0x00FFFFFF);
        if (focused) wm_surface_draw_rect(win_id, x + 8 + cursor_pos * 8, y + 4, 1, 16, 0x0038BDF8);
    } else {
        wm_surface_draw_text(win_id, "Spotlight...", x + 8, y + 5, 0x0064748B);
        if (focused) wm_surface_draw_rect(win_id, x + 8 + cursor_pos * 8, y + 4, 1, 16, 0x0038BDF8);
    }
}

void wm_surface_draw_command_bar(int win_id, int y, int w) {
    wm_surface_draw_gradient(win_id, 0, y, w, 28, 0x001E293B, 0x000F172A);
    wm_surface_draw_rect(win_id, 0, y, w, 1, 0x00334155);
    wm_surface_draw_rect(win_id, 0, y + 27, w, 1, 0x00020617);

    wm_surface_draw_text(win_id, "+Folder", 10, y + 6, 0x004ADE80);
    wm_surface_draw_rect(win_id, 70, y + 4, 1, 20, 0x00334155);
    wm_surface_draw_text(win_id, "+File", 78, y + 6, 0x004ADE80);
    wm_surface_draw_rect(win_id, 126, y + 4, 1, 20, 0x00334155);
    wm_surface_draw_text(win_id, "Copy", 134, y + 6, 0x00FFFFFF);
    wm_surface_draw_rect(win_id, 172, y + 4, 1, 20, 0x00334155);
    wm_surface_draw_text(win_id, "Cut", 180, y + 6, 0x00FFFFFF);
    wm_surface_draw_rect(win_id, 210, y + 4, 1, 20, 0x00334155);
    wm_surface_draw_text(win_id, "Paste", 218, y + 6, 0x00FFFFFF);
    wm_surface_draw_rect(win_id, 264, y + 4, 1, 20, 0x00334155);
    wm_surface_draw_text(win_id, "Rename", 272, y + 6, 0x0038BDF8);
    wm_surface_draw_rect(win_id, 326, y + 4, 1, 20, 0x00334155);
    wm_surface_draw_text(win_id, "Delete", 334, y + 6, 0x00F87171);
}

void wm_surface_draw_modal_dialog(int win_id, int x, int y, int w, int h, const char *title) {
    wm_surface_draw_rect(win_id, x - 3, y - 3, w + 6, h + 6, 0x00020617);
    wm_surface_draw_gradient(win_id, x, y, w, h, 0x001E293B, 0x000F172A);
    wm_surface_draw_rect(win_id, x, y, w, 1, 0x0038BDF8);
    wm_surface_draw_rect(win_id, x, y + h - 1, w, 1, 0x00334155);
    wm_surface_draw_gradient(win_id, x, y, w, 32, 0x000284C7, 0x000369A1);
    if (title) wm_surface_draw_text(win_id, title, x + 14, y + 8, 0x00FFFFFF);
}

void wm_init(void) {
    g_window_count = 0;
    g_z_count = 0;
    g_active_win_id = -1;
    g_drag_win_id = -1;

    for (int i = 0; i < MAX_WINDOWS; i++) {
        g_windows[i].is_closed = 1;
        g_windows[i].on_click = NULL;
        g_windows[i].on_key = NULL;
        g_windows[i].ev_q_head = 0;
        g_windows[i].ev_q_tail = 0;
        g_windows[i].scroll_x = 0;
        g_windows[i].scroll_y = 0;
        g_windows[i].content_w = 0;
        g_windows[i].content_h = 0;
        g_windows[i].auto_scroll = 1;
        g_windows[i].surface = g_win_surfaces[i];
        g_windows[i].anim_state = ANIM_NONE;
        g_windows[i].anim_progress = 100;
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

    if (!g_win_surfaces[slot]) {
        UINTN surf_bytes = (UINTN)SURFACE_STRIDE * SURFACE_HEIGHT * sizeof(UINT32);
        UINTN pages = EFI_SIZE_TO_PAGES(surf_bytes);
        EFI_PHYSICAL_ADDRESS phys = 0;
        if (BS && BS->AllocatePages) {
            EFI_STATUS status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, pages, &phys);
            if (status == EFI_SUCCESS && phys != 0) g_win_surfaces[slot] = (UINT32*)(UINTN)phys;
        }
        if (!g_win_surfaces[slot]) {
            g_win_surfaces[slot] = (UINT32*)AllocateZeroPool(surf_bytes);
        }
    }

    Window *win = &g_windows[slot];
    win->id = slot;
    win->x = x;
    win->y = (y < 24) ? 24 : y;
    win->width = w;
    win->height = h;
    win->orig_x = win->x;
    win->orig_y = win->y;
    win->orig_w = w;
    win->orig_h = h;
    win->is_active = 1;
    win->is_minimized = 0;
    win->is_maximized = 0;
    win->is_closed = 0;
    win->is_dirty_content = 1;
    win->bg_color = 0x000F172A;
    win->surface = g_win_surfaces[slot];
    win->scroll_x = 0;
    win->scroll_y = 0;
    win->content_w = w - 12;
    win->content_h = h - 36;
    win->auto_scroll = 1;
    win->ev_q_head = 0;
    win->ev_q_tail = 0;
    win->on_paint = on_paint;
    win->on_click = NULL;
    win->on_key = NULL;

    /* Pop-In Startanimation */
    win->anim_state = ANIM_OPENING;
    win->anim_progress = 10;

    int cw = w - 12, ch = h - 36;
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
    int max_avail_w = (int)gop_width - 40, max_avail_h = (int)gop_height - 90;
    if (content_w > max_avail_w) content_w = max_avail_w;
    if (content_h > max_avail_h) content_h = max_avail_h;

    int total_w = content_w + 12;
    if (total_w < 260) total_w = 260;
    int total_h = content_h + 36;
    int x = ((int)gop_width - total_w) / 2, y = (24 + ((int)gop_height - 80 - total_h) / 2);
    if (x < 10) x = 10;
    if (y < 28) y = 28;

    return wm_create_window(x, y, total_w, total_h, title, on_paint);
}

void wm_close_window(int id) {
    if (id < 0 || id >= MAX_WINDOWS || g_windows[id].is_closed) return;
    g_windows[id].is_closed = 1;
    g_windows[id].is_active = 0;
    g_windows[id].anim_state = ANIM_NONE;
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

void wm_minimize_window_to(int id, int dock_x, int dock_y) {
    if (id < 0 || id >= MAX_WINDOWS || g_windows[id].is_closed) return;
    Window *win = &g_windows[id];

    if (!win->is_minimized) {
        win->target_dock_x = dock_x;
        win->target_dock_y = dock_y;
        win->anim_state = ANIM_MINIMIZE;
        win->anim_progress = 0;
        win->is_active = 0;
    } else {
        win->is_minimized = 0;
        win->anim_state = ANIM_RESTORE;
        win->anim_progress = 0;
        wm_focus_window(id);
    }
    wm_mark_all_dirty();
}

void wm_minimize_window(int id) {
    wm_minimize_window_to(id, (int)gop_width / 2, (int)gop_height - 30);
}

void wm_minimize_active(void) {
    if (g_active_win_id >= 0) wm_minimize_window(g_active_win_id);
}

void wm_minimize_all(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!g_windows[i].is_closed && !g_windows[i].is_minimized) {
            wm_minimize_window(i);
        }
    }
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
        win->y = 24;
        new_w = (int)gop_width;
        new_h = (int)gop_height - 24;
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

int wm_tick_animations(void) {
    int active_anims = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        Window *win = &g_windows[i];
        if (win->is_closed || win->anim_state == ANIM_NONE) continue;

        active_anims = 1;
        win->anim_progress += 20;

        if (win->anim_progress >= 100) {
            if (win->anim_state == ANIM_MINIMIZE) {
                win->is_minimized = 1;
            }
            win->anim_state = ANIM_NONE;
            win->anim_progress = 100;
        }
    }
    if (active_anims) wm_mark_all_dirty();
    return active_anims;
}

Window* wm_get_window(int id) {
    if (id < 0 || id >= MAX_WINDOWS || g_windows[id].is_closed) return NULL;
    return &g_windows[id];
}

int wm_get_window_count(void) {
    return g_window_count;
}

int wm_get_active_window_id(void) {
    return g_active_win_id;
}

int wm_get_z_count(void) {
    return g_z_count;
}

int wm_get_z_window(int z_idx) {
    if (z_idx < 0 || z_idx >= g_z_count) return -1;
    return g_z_order[z_idx];
}

/* =========================================================================
 * RENDERING: KNÖPFE OBEN RECHTS & TITEL LINKS MIT HOHEM KONTRAST
 * ========================================================================= */
static void render_window_frame(Window *win) {
    if (win->is_closed || (win->is_minimized && win->anim_state == ANIM_NONE)) return;

    int x = win->x, y = win->y, w = win->width, h = win->height;

    if (win->anim_state == ANIM_MINIMIZE) {
        int p = win->anim_progress;
        x = win->x + ((win->target_dock_x - win->x) * p) / 100;
        y = win->y + ((win->target_dock_y - win->y) * p) / 100;
        w = win->width - ((win->width - 40) * p) / 100;
        h = win->height - ((win->height - 40) * p) / 100;
    } else if (win->anim_state == ANIM_RESTORE) {
        int p = 100 - win->anim_progress;
        x = win->x + ((win->target_dock_x - win->x) * p) / 100;
        y = win->y + ((win->target_dock_y - win->y) * p) / 100;
        w = win->width - ((win->width - 40) * p) / 100;
        h = win->height - ((win->height - 40) * p) / 100;
    } else if (win->anim_state == ANIM_OPENING) {
        int p = win->anim_progress;
        int diff_w = (win->width * (100 - p)) / 200;
        int diff_h = (win->height * (100 - p)) / 200;
        x = win->x + diff_w;
        y = win->y + diff_h;
        w = win->width - diff_w * 2;
        h = win->height - diff_h * 2;
    }

    if (w < 80) w = 80;
    if (h < 40) h = 40;

    if (!win->is_maximized && win->anim_state == ANIM_NONE) {
        draw_shadow_box(x, y, w, h, 10);
        UINT32 border_col = win->is_active ? 0x0038BDF8 : 0x00334155;
        draw_rounded_rect_aa(x, y, w, h, 12, border_col);
    } else if (win->anim_state != ANIM_NONE) {
        draw_rounded_rect_aa(x - 1, y - 1, w + 2, h + 2, 10, 0x0038BDF8);
    }

    // Titelleiste
    UINT32 top_glass = win->is_active ? 0x001E293B : 0x000F172A;
    UINT32 bot_glass = win->is_active ? 0x000F172A : 0x00020617;
    int title_r = win->is_maximized ? 0 : 11;
    draw_rounded_rect_gradient(x, y, w, 30, title_r, top_glass, bot_glass);
    draw_filled_rect((UINTN)x, (UINTN)(y + 29), (UINTN)w, 1, win->is_active ? 0x00334155 : 0x001E293B);

    // Knöpfe OBEN RECHTS: Rot (Schließen), Grün (Maximieren), Gelb (Minimieren)
    int btn_cy = y + 15;
    draw_circle_button_aa(x + w - 18, btn_cy, 6, 0x00EF4444, 0x00F87171);
    draw_circle_button_aa(x + w - 38, btn_cy, 6, 0x0010B981, 0x0034D399);
    draw_circle_button_aa(x + w - 58, btn_cy, 6, 0x00F59E0B, 0x00FBBF24);

    // Titel links mit Kontrastprüfung
    UINT32 title_fg = wm_get_contrast_color(top_glass);
    wm_draw_text_shadow(win->title, x + 14, y + 7, 1, title_fg, 0x00020617);

    int off_x = win->is_maximized ? 0 : 6, off_y = 30;
    int cw = w - 12;
    int ch = h - 36;
    if (cw < 10) cw = 10;
    if (ch < 10) ch = 10;

    int has_v_scroll = (win->auto_scroll && win->content_h > ch && win->anim_state == ANIM_NONE);
    int sb_w = has_v_scroll ? 14 : 0;
    int view_w = cw - sb_w;

    int max_scroll_y = win->content_h - ch;
    if (max_scroll_y < 0) max_scroll_y = 0;
    if (win->scroll_y < 0) win->scroll_y = 0;
    if (win->scroll_y > max_scroll_y) win->scroll_y = max_scroll_y;

    if (win->surface && view_w > 0 && ch > 0 && g_backbuffer) {
        int dst_x = x + off_x, dst_y = y + off_y;
        int src_x = win->scroll_x, draw_w = view_w;

        if (dst_x < 0) { src_x += -dst_x; draw_w += dst_x; dst_x = 0; }
        if (dst_x + draw_w > (int)gop_width) draw_w = (int)gop_width - dst_x;

        if (draw_w > 0) {
            UINTN copy_bytes = (UINTN)draw_w * sizeof(UINT32);
            for (int cy = 0; cy < ch; cy++) {
                int dy = dst_y + cy;
                if (dy < 0 || dy >= (int)gop_height) continue;
                int sy = cy + win->scroll_y;
                if (sy < 0 || sy >= SURFACE_HEIGHT) continue;

                UINT32 *dst_row = &g_backbuffer[dy * gop_width + dst_x];
                const UINT32 *src_row = &win->surface[sy * SURFACE_STRIDE + src_x];
                __builtin_memcpy(dst_row, src_row, copy_bytes);
            }
        }
    }

    if (has_v_scroll && sb_w > 0) {
        int sb_x = x + off_x + view_w;
        int sb_y = y + off_y;
        draw_rounded_rect_gradient(sb_x, sb_y, sb_w, ch, 0, 0x000F172A, 0x00020617);
        draw_line_aa(sb_x, sb_y, sb_x, sb_y + ch, 1, 0x001E293B);

        int thumb_h = (ch * ch) / win->content_h;
        if (thumb_h < 24) thumb_h = 24;
        if (thumb_h > ch) thumb_h = ch;

        int thumb_y = sb_y + (max_scroll_y > 0 ? (win->scroll_y * (ch - thumb_h)) / max_scroll_y : 0);
        if (thumb_y + thumb_h > sb_y + ch) thumb_y = sb_y + ch - thumb_h;

        draw_rounded_rect_gradient(sb_x + 2, thumb_y, sb_w - 4, thumb_h, 4, 0x0038BDF8, 0x000284C7);
    }

    if (win->on_paint && win->anim_state == ANIM_NONE) {
        win->on_paint(win->id, x + off_x, y + off_y, cw, ch);
    }
}

void wm_render_all(void) {
    for (int i = 0; i < g_z_count; i++) {
        int id = g_z_order[i];
        if (id >= 0 && id < MAX_WINDOWS && !g_windows[id].is_closed) {
            render_window_frame(&g_windows[id]);
        }
    }
}
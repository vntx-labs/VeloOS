// wm.c - VeloOS Dedicated Window Manager mit analytischem Anti-Aliasing
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

/* Hardwarebeschleunigte SSE-Quadratwurzel für 64-Bit Subpixel-Präzision */
static inline double math_sqrt(double x) {
    double res;
    __asm__ volatile("sqrtsd %1, %0" : "=x"(res) : "x"(x));
    return res;
}

/* 3. Anti-Aliasing Alpha-Blending (0..255) */
UINT32 alpha_blend(UINT32 fg, UINT32 bg, UINT32 alpha) {
    if (alpha >= 255) return fg;
    if (alpha == 0) return bg;
    UINT32 inv = 255 - alpha;
    UINT32 r = (((fg >> 16) & 0xFF) * alpha + ((bg >> 16) & 0xFF) * inv) / 255;
    UINT32 g = (((fg >> 8) & 0xFF) * alpha + ((bg >> 8) & 0xFF) * inv) / 255;
    UINT32 b = ((fg & 0xFF) * alpha + (bg & 0xFF) * inv) / 255;
    return (r << 16) | (g << 8) | b;
}

/* Exakte Stringvermessung */
void wm_measure_string(const char *str, int scale, int *out_w, int *out_h) {
    if (scale <= 0) scale = 1;
    int max_line_len = 0;
    int current_len = 0;
    int line_count = 1;

    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            if (current_len > max_line_len) max_line_len = current_len;
            current_len = 0;
            line_count++;
        } else {
            current_len++;
        }
    }
    if (current_len > max_line_len) max_line_len = current_len;

    if (out_w) *out_w = max_line_len * (8 * scale);
    if (out_h) *out_h = line_count * (16 * scale + 6);
}

/* Exakte Zeilenvermessung */
void wm_measure_text_lines(const char **lines, int line_count, int scale, int *out_w, int *out_h) {
    if (scale <= 0) scale = 1;
    int max_len = 0;
    for (int i = 0; i < line_count; i++) {
        if (!lines[i]) continue;
        int len = 0;
        while (lines[i][len] != '\0') len++;
        if (len > max_len) max_len = len;
    }
    if (out_w) *out_w = max_len * (8 * scale);
    if (out_h) *out_h = line_count * (16 * scale + 8);
}

/* Hardware-geclippter Glyphen-Zeichner */
static void draw_char_clipped(char c, int x, int y, int scale, UINT32 fg_color, UINT32 bg_color, int cx, int cy, int cw, int ch) {
    const unsigned char* glyph = font8x16[(unsigned char)c];
    for (int gy = 0; gy < 16; gy++) {
        unsigned char line = glyph[gy];
        for (int gx = 0; gx < 8; gx++) {
            UINT32 col = (line & (1 << (7 - gx))) ? fg_color : bg_color;
            if (col == 0x00000000 && !(line & (1 << (7 - gx)))) continue;

            for (int sy = 0; sy < scale; sy++) {
                for (int sx = 0; sx < scale; sx++) {
                    int px = x + (gx * scale) + sx;
                    int py = y + (gy * scale) + sy;

                    /* Absolutes Clipping gegen Fensterkanten */
                    if (px >= cx && px < cx + cw && py >= cy && py < cy + ch) {
                        put_pixel(px, py, col);
                    }
                }
            }
        }
    }
}

/* Hardware-geclippter String-Zeichner für Fensterinhalt */
void wm_draw_string_content(const char *str, int x, int y, int scale, UINT32 fg_color, UINT32 bg_color, int cx, int cy, int cw, int ch) {
    if (scale <= 0) scale = 1;
    int char_w = 8 * scale;
    int line_h = 16 * scale + 8;
    int cur_x = x;
    int cur_y = y;

    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            cur_x = x;
            cur_y += line_h;
        } else if (str[i] == '\t') {
            cur_x += char_w * 4;
        } else {
            draw_char_clipped(str[i], cur_x, cur_y, scale, fg_color, bg_color, cx, cy, cw, ch);
            cur_x += char_w;
        }
    }
}

/* 2. Analytische Anti-Aliased Vektor-Linie */
void draw_line_aa(int x1, int y1, int x2, int y2, int thickness, UINT32 color) {
    double vx = (double)(x2 - x1);
    double vy = (double)(y2 - y1);
    double len_sq = vx * vx + vy * vy;
    if (len_sq <= 0.0001) return;

    double half_th = (double)thickness / 2.0;
    int margin = (int)(half_th + 3.0);

    int min_x = (x1 < x2 ? x1 : x2) - margin;
    int max_x = (x1 > x2 ? x1 : x2) + margin;
    int min_y = (y1 < y2 ? y1 : y2) - margin;
    int max_y = (y1 > y2 ? y1 : y2) + margin;

    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= (int)gop_width) max_x = (int)gop_width - 1;
    if (max_y >= (int)gop_height) max_y = (int)gop_height - 1;

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            double dx_a = (double)x - (double)x1;
            double dy_a = (double)y - (double)y1;
            double t = (dx_a * vx + dy_a * vy) / len_sq;
            if (t < 0.0) t = 0.0;
            else if (t > 1.0) t = 1.0;

            double qx = (double)x1 + t * vx;
            double qy = (double)y1 + t * vy;

            double dx = (double)x - qx;
            double dy = (double)y - qy;
            double dist = math_sqrt(dx * dx + dy * dy);

            double cov = half_th + 0.5 - dist;
            if (cov >= 1.0) {
                put_pixel(x, y, color);
            } else if (cov > 0.0) {
                UINT32 alpha = (UINT32)(cov * 255.0);
                UINT32 bg = get_pixel(x, y);
                put_pixel(x, y, alpha_blend(color, bg, alpha));
            }
        }
    }
}

/* 2. Analytischer Anti-Aliased Kreisbogen */
void draw_circle_aa(int cx, int cy, int radius, int thickness, UINT32 color) {
    double half_th = (double)thickness / 2.0;
    double r_in = (double)radius - half_th;
    double r_out = (double)radius + half_th;
    int margin = (int)(r_out + 3.0);

    int min_x = cx - margin > 0 ? cx - margin : 0;
    int max_x = cx + margin < (int)gop_width ? cx + margin : (int)gop_width - 1;
    int min_y = cy - margin > 0 ? cy - margin : 0;
    int max_y = cy + margin < (int)gop_height ? cy + margin : (int)gop_height - 1;

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            double dx = (double)x - (double)cx;
            double dy = (double)y - (double)cy;
            double d = math_sqrt(dx * dx + dy * dy);

            double c_in = d - r_in + 0.5;
            if (c_in < 0.0) c_in = 0.0;
            else if (c_in > 1.0) c_in = 1.0;

            double c_out = r_out + 0.5 - d;
            if (c_out < 0.0) c_out = 0.0;
            else if (c_out > 1.0) c_out = 1.0;

            double cov = (c_in < c_out) ? c_in : c_out;
            if (cov >= 1.0) {
                put_pixel(x, y, color);
            } else if (cov > 0.0) {
                UINT32 alpha = (UINT32)(cov * 255.0);
                UINT32 bg = get_pixel(x, y);
                put_pixel(x, y, alpha_blend(color, bg, alpha));
            }
        }
    }
}

/* 2. Analytischer Anti-Aliased ausgefüllter Kreis */
void draw_filled_circle_aa(int cx, int cy, int radius, UINT32 color) {
    int margin = radius + 3;
    int min_x = cx - margin > 0 ? cx - margin : 0;
    int max_x = cx + margin < (int)gop_width ? cx + margin : (int)gop_width - 1;
    int min_y = cy - margin > 0 ? cy - margin : 0;
    int max_y = cy + margin < (int)gop_height ? cy + margin : (int)gop_height - 1;

    double r_f = (double)radius;

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            double dx = (double)x - (double)cx;
            double dy = (double)y - (double)cy;
            double d = math_sqrt(dx * dx + dy * dy);

            double cov = r_f + 0.5 - d;
            if (cov >= 1.0) {
                put_pixel(x, y, color);
            } else if (cov > 0.0) {
                UINT32 alpha = (UINT32)(cov * 255.0);
                UINT32 bg = get_pixel(x, y);
                put_pixel(x, y, alpha_blend(color, bg, alpha));
            }
        }
    }
}

/* Abgerundeter Kasten mit Subpixel-Anti-Aliasing */
void draw_rounded_rect_aa(int sx, int sy, int w, int h, int r, UINT32 color) {
    if (r <= 0) {
        draw_filled_rect(sx, sy, w, h, color);
        return;
    }
    double r_f = (double)r;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int px = sx + x;
            int py = sy + y;
            if (px < 0 || py < 0 || px >= (int)gop_width || py >= (int)gop_height) continue;

            double cx_c = 0.0, cy_c = 0.0;
            int in_corner = 0;

            if (x < r && y < r) { cx_c = r_f; cy_c = r_f; in_corner = 1; }
            else if (x >= w - r && y < r) { cx_c = (double)(w - r - 1); cy_c = r_f; in_corner = 1; }
            else if (x < r && y >= h - r) { cx_c = r_f; cy_c = (double)(h - r - 1); in_corner = 1; }
            else if (x >= w - r && y >= h - r) { cx_c = (double)(w - r - 1); cy_c = (double)(h - r - 1); in_corner = 1; }

            if (!in_corner) {
                put_pixel(px, py, color);
            } else {
                double dx = (double)x - cx_c;
                double dy = (double)y - cy_c;
                double d = math_sqrt(dx * dx + dy * dy);

                double cov = r_f + 0.5 - d;
                if (cov >= 1.0) {
                    put_pixel(px, py, color);
                } else if (cov > 0.0) {
                    UINT32 alpha = (UINT32)(cov * 255.0);
                    UINT32 bg = get_pixel(px, py);
                    put_pixel(px, py, alpha_blend(color, bg, alpha));
                }
            }
        }
    }
}

/* Farbverlauf mit abgerundeten Ecken */
void draw_rounded_rect_gradient(int sx, int sy, int w, int h, int r, UINT32 top_col, UINT32 bot_col) {
    UINT32 tr = (top_col >> 16) & 0xFF, tg = (top_col >> 8) & 0xFF, tb = top_col & 0xFF;
    UINT32 br = (bot_col >> 16) & 0xFF, bg_val = (bot_col >> 8) & 0xFF, bb = bot_col & 0xFF;
    double r_f = (double)r;

    for (int y = 0; y < h; y++) {
        UINT32 cr = tr + ((br - tr) * y) / (h ? h : 1);
        UINT32 cg = tg + ((bg_val - tg) * y) / (h ? h : 1);
        UINT32 cb = tb + ((bb - tb) * y) / (h ? h : 1);
        UINT32 col = (cr << 16) | (cg << 8) | cb;

        for (int x = 0; x < w; x++) {
            int px = sx + x;
            int py = sy + y;
            if (px < 0 || py < 0 || px >= (int)gop_width || py >= (int)gop_height) continue;

            if (r <= 0) {
                put_pixel(px, py, col);
                continue;
            }

            double cx_c = 0.0, cy_c = 0.0;
            int in_corner = 0;

            if (x < r && y < r) { cx_c = r_f; cy_c = r_f; in_corner = 1; }
            else if (x >= w - r && y < r) { cx_c = (double)(w - r - 1); cy_c = r_f; in_corner = 1; }
            else if (x < r && y >= h - r) { cx_c = r_f; cy_c = (double)(h - r - 1); in_corner = 1; }
            else if (x >= w - r && y >= h - r) { cx_c = (double)(w - r - 1); cy_c = (double)(h - r - 1); in_corner = 1; }

            if (!in_corner) {
                put_pixel(px, py, col);
            } else {
                double dx = (double)x - cx_c;
                double dy = (double)y - cy_c;
                double d = math_sqrt(dx * dx + dy * dy);

                double cov = r_f + 0.5 - d;
                if (cov >= 1.0) {
                    put_pixel(px, py, col);
                } else if (cov > 0.0) {
                    UINT32 alpha = (UINT32)(cov * 255.0);
                    UINT32 bg = get_pixel(px, py);
                    put_pixel(px, py, alpha_blend(col, bg, alpha));
                }
            }
        }
    }
}

void wm_init(void) {
    g_window_count = 0;
    g_active_win_id = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        g_windows[i].is_closed = 1;
    }
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
    win->bg_color = 0x00141620;
    win->on_paint = on_paint;

    int p = 0;
    while (title[p] && p < 63) {
        win->title[p] = title[p];
        p++;
    }
    win->title[p] = '\0';

    wm_focus_window(slot);
    g_window_count++;
    return slot;
}

/* Automatisch skaliertes und zentriertes Fenster erstellen */
int wm_create_window_auto(const char *title, int content_w, int content_h, void (*on_paint)(int, int, int, int, int)) {
    int min_w = 280;
    int pad_x = 36;
    int pad_y = 28;
    int titlebar_h = 36;

    int total_w = content_w + pad_x;
    if (total_w < min_w) total_w = min_w;
    int total_h = titlebar_h + content_h + pad_y;

    /* Clamping gegen Bildschirmgröße */
    if (total_w > (int)gop_width - 40) total_w = (int)gop_width - 40;
    if (total_h > (int)gop_height - 90) total_h = (int)gop_height - 90;

    /* Exakte Zentrierung */
    int x = ((int)gop_width - total_w) / 2;
    int y = ((int)gop_height - 44 - total_h) / 2;
    if (y < 34) y = 34;

    return wm_create_window(x, y, total_w, total_h, title, on_paint);
}

/* Erstellt ein Fenster exakt passend zu einem Textzeilen-Array */
int wm_create_window_for_lines(const char *title, const char **lines, int line_count, int scale, void (*on_paint)(int, int, int, int, int)) {
    int cw = 0, ch = 0;
    wm_measure_text_lines(lines, line_count, scale, &cw, &ch);
    return wm_create_window_auto(title, cw, ch, on_paint);
}

void wm_close_window(int id) {
    if (id < 0 || id >= MAX_WINDOWS || g_windows[id].is_closed) return;
    g_windows[id].is_closed = 1;
    g_window_count--;
    if (g_active_win_id == id) {
        wm_focus_next();
    }
}

void wm_close_active(void) {
    if (g_active_win_id >= 0) {
        wm_close_window(g_active_win_id);
    }
}

void wm_minimize_window(int id) {
    if (id < 0 || id >= MAX_WINDOWS || g_windows[id].is_closed) return;
    g_windows[id].is_minimized = !g_windows[id].is_minimized;
    if (g_windows[id].is_minimized && g_active_win_id == id) {
        wm_focus_next();
    }
}

void wm_minimize_active(void) {
    if (g_active_win_id >= 0) {
        wm_minimize_window(g_active_win_id);
    }
}

void wm_focus_window(int id) {
    if (id < 0 || id >= MAX_WINDOWS || g_windows[id].is_closed) return;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        g_windows[i].is_active = (i == id);
    }
    g_windows[id].is_minimized = 0;
    g_active_win_id = id;
}

void wm_focus_next(void) {
    if (g_window_count <= 0) {
        g_active_win_id = -1;
        return;
    }
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

int wm_get_window_count(void) {
    return g_window_count;
}

int wm_get_active_window_id(void) {
    return g_active_win_id;
}

static void render_window_frame(Window *win) {
    if (win->is_closed || win->is_minimized) return;

    int x = win->x;
    int y = win->y;
    int w = win->width;
    int h = win->height;

    /* 1. Weicher Schattenwurf */
    draw_rounded_rect_aa(x + 6, y + 6, w, h, 12, 0x00060810);

    /* 2. Abgerundeter Fenster-Hauptkörper */
    draw_rounded_rect_aa(x, y, w, h, 10, win->bg_color);

    /* 3. Titelleiste mit weichem Farbverlauf */
    UINT32 tb_top = win->is_active ? 0x001B62D6 : 0x002B2E3D;
    UINT32 tb_bot = win->is_active ? 0x000E429C : 0x001B1D26;
    draw_rounded_rect_gradient(x, y, w, 36, 10, tb_top, tb_bot);
    draw_filled_rect(x, y + 26, w, 10, tb_bot);

    /* Titelleisten-Text */
    wm_draw_string_content(win->title, x + 16, y + 10, 1, 0x00FFFFFF, tb_bot, x, y, w - 70, 36);

    /* Window Control Buttons */
    draw_rounded_rect_aa(x + w - 32, y + 6, 24, 24, 6, 0x00E06C75);
    wm_draw_string_content("X", x + w - 24, y + 10, 1, 0x00FFFFFF, 0x00E06C75, x, y, w, h);

    draw_rounded_rect_aa(x + w - 62, y + 6, 24, 24, 6, 0x003B4261);
    wm_draw_string_content("-", x + w - 54, y + 10, 1, 0x00FFFFFF, 0x003B4261, x, y, w, h);

    /* 4. Fensterinhalt mit festen inneren Rändern rendern */
    if (win->on_paint) {
        win->on_paint(win->id, x + 16, y + 46, w - 32, h - 56);
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
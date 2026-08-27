#ifndef _VELO_ICONS_H
#define _VELO_ICONS_H

typedef void (*pixel_setter_fn)(int x, int y, unsigned int color);

#define SCALE_COORD(val, size) (((val) * (size)) / 32)

#define VELO_ICON_PC       1
#define VELO_ICON_FOLDER   2
#define VELO_ICON_DOC      3
#define VELO_ICON_UNKNOWN  4
#define VELO_ICON_APP      5

#define COLOR_VANTIX_BLUE   0x00000080
#define COLOR_FOLDER_YELLOW 0x00FACC15
#define COLOR_FRAME_GRAY    0x007F7F7F

static inline unsigned int blend_col(unsigned int fg, unsigned int bg, unsigned int a) {
    if (a >= 255) return fg;
    if (a == 0) return bg;
    unsigned int inv = 255 - a;
    unsigned int r = (((fg >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * inv) >> 8;
    unsigned int g = (((fg >> 8) & 0xFF) * a + ((bg >> 8) & 0xFF) * inv) >> 8;
    unsigned int b = ((fg & 0xFF) * a + (bg & 0xFF) * inv) >> 8;
    return (r << 16) | (g << 8) | b;
}

/**
 * 100% reine Integer-Arithmetik (keine Floats, keine FPU/SSE-Traps!)
 */
static inline int is_in_vantix_curve(int lx, int ly, int size) {
    if (size <= 0) return 0;
    int local_x = (lx * 32) / size;
    int local_y = (ly * 32) / size;

    if (local_x < 2 || local_x > 30) return 0;

    // Feste Festkomma-Arithmetik (skaliert mit 1000)
    int upper = (9000 + local_x * 450 - (local_x * local_x * 18)) / 1000;
    int lower = (7000 + local_x * 550 - (local_x * local_x * 24)) / 1000;

    return (local_y >= lower && local_y <= upper);
}

// ----------------------------------------------------
// 1. PC / COMPUTER ICON
// ----------------------------------------------------
static inline void render_icon_pc(int x, int y, int size, pixel_setter_fn set_px) {
    if (!set_px || size <= 0) return;

    int mon_y1 = y + SCALE_COORD(2, size), mon_y2 = y + SCALE_COORD(22, size);
    int mon_x1 = x + SCALE_COORD(2, size), mon_x2 = x + SCALE_COORD(30, size);

    for (int cy = mon_y1; cy <= mon_y2; cy++) {
        for (int cx = mon_x1; cx <= mon_x2; cx++) {
            unsigned int c = (cy == mon_y1 || cy == mon_y2 || cx == mon_x1 || cx == mon_x2) ? 0x00475569 : 0x000F172A;
            set_px(cx, cy, c);
        }
    }

    int scr_y1 = y + SCALE_COORD(4, size), scr_y2 = y + SCALE_COORD(20, size);
    int scr_x1 = x + SCALE_COORD(4, size), scr_x2 = x + SCALE_COORD(28, size);
    for (int cy = scr_y1; cy <= scr_y2; cy++) {
        for (int cx = scr_x1; cx <= scr_x2; cx++) {
            unsigned int c = (cy < (scr_y1 + scr_y2) / 2) ? 0x0038BDF8 : 0x000284C7;
            if (cx - scr_x1 == cy - scr_y1) c = 0x00BAE6FD;
            set_px(cx, cy, c);
        }
    }

    int st_y1 = y + SCALE_COORD(23, size), st_y2 = y + SCALE_COORD(26, size);
    int st_x1 = x + SCALE_COORD(13, size), st_x2 = x + SCALE_COORD(18, size);
    for (int cy = st_y1; cy <= st_y2; cy++) {
        for (int cx = st_x1; cx <= st_x2; cx++) set_px(cx, cy, 0x0064748B);
    }
    int b_y1 = y + SCALE_COORD(27, size), b_y2 = y + SCALE_COORD(29, size);
    int b_x1 = x + SCALE_COORD(8, size), b_x2 = x + SCALE_COORD(24, size);
    for (int cy = b_y1; cy <= b_y2; cy++) {
        for (int cx = b_x1; cx <= b_x2; cx++) set_px(cx, cy, 0x0094A3B8);
    }
    set_px(x + SCALE_COORD(16, size), y + SCALE_COORD(21, size), 0x004ADE80);
}

// ----------------------------------------------------
// 2. FOLDER SVG ICON
// ----------------------------------------------------
static inline void render_svg_folder(int x, int y, int size, pixel_setter_fn set_px) {
    if (!set_px || size <= 0) return;

    int x1 = x, x2 = x + size - 1;
    int y1 = y, y2 = y + size - 1;

    int border_thickness = SCALE_COORD(2, size);
    if (border_thickness < 1) border_thickness = 1;

    for (int cy = y1; cy <= y2; cy++) {
        int ly = cy - y1;
        for (int cx = x1; cx <= x2; cx++) {
            int lx = cx - x1;

            if (lx < border_thickness || lx > size - 1 - border_thickness || ly > size - 1 - border_thickness) {
                set_px(cx, cy, COLOR_FRAME_GRAY);
            } else {
                unsigned int col = is_in_vantix_curve(lx, ly, size) ? COLOR_VANTIX_BLUE : COLOR_FOLDER_YELLOW;
                set_px(cx, cy, col);
            }
        }
    }
}

// ----------------------------------------------------
// 3. FILES / DOCS SVG ICON
// ----------------------------------------------------
static inline void render_svg_doc(int x, int y, int size, pixel_setter_fn set_px) {
    if (!set_px || size <= 0) return;

    for (int cy = y; cy < y + size; cy++) {
        int ly = cy - y;
        for (int cx = x; cx < x + size; cx++) {
            int lx = cx - x;
            if (is_in_vantix_curve(lx, ly, size)) {
                set_px(cx, cy, COLOR_VANTIX_BLUE);
            }
        }
    }
}

// ----------------------------------------------------
// 4. UNKNOWN FILE SVG ICON
// ----------------------------------------------------
static inline void render_svg_unknown(int x, int y, int size, pixel_setter_fn set_px) {
    if (!set_px || size <= 0) return;

    int x1 = x + SCALE_COORD(4, size), x2 = x + SCALE_COORD(27, size);
    int y1 = y + SCALE_COORD(2, size), y2 = y + SCALE_COORD(29, size);
    int fold_size = SCALE_COORD(7, size);

    for (int cy = y1; cy <= y2; cy++) {
        for (int cx = x1; cx <= x2; cx++) {
            if (cx >= x2 - fold_size && cy <= y1 + fold_size && (cx - (x2 - fold_size)) > (fold_size - (cy - y1))) {
                continue;
            }
            unsigned int c = (cy == y1 || cy == y2 || cx == x1 || cx == x2) ? 0x0064748B : 0x00F1F5F9;
            set_px(cx, cy, c);
        }
    }

    int fx = x2 - fold_size;
    int fy = y1 + fold_size;
    for (int cy = y1; cy <= fy; cy++) {
        for (int cx = fx; cx <= x2; cx++) {
            if ((cx - fx) <= (cy - y1)) {
                set_px(cx, cy, 0x00CBD5E1);
            }
        }
    }

    int qx = x + SCALE_COORD(15, size);
    int qy = y + SCALE_COORD(14, size);
    int qs = SCALE_COORD(2, size);
    if (qs < 1) qs = 1;

    for (int i = -qs; i <= qs; i++) {
        set_px(qx + i, qy - qs, 0x00475569);
        set_px(qx + qs, qy + i - qs, 0x00475569);
        set_px(qx, qy + i, 0x00475569);
        set_px(qx, qy + qs + 2, 0x00475569);
    }
}

// ----------------------------------------------------
// 5. APPLICATION BINARY ICON
// ----------------------------------------------------
static inline void render_svg_app(int x, int y, int size, pixel_setter_fn set_px) {
    if (!set_px || size <= 0) return;

    for (int cy = y; cy < y + size; cy++) {
        int ly = cy - y;
        for (int cx = x; cx < x + size; cx++) {
            int lx = cx - x;
            unsigned int col = is_in_vantix_curve(lx, ly, size) ? COLOR_VANTIX_BLUE : COLOR_FOLDER_YELLOW;
            set_px(cx, cy, col);
        }
    }
}

static inline void render_icon_pc_32(int x, int y, pixel_setter_fn set_px) { render_icon_pc(x, y, 32, set_px); }
static inline void render_icon_folder_32(int x, int y, pixel_setter_fn set_px) { render_svg_folder(x, y, 32, set_px); }
static inline void render_icon_doc_32(int x, int y, pixel_setter_fn set_px) { render_svg_doc(x, y, 32, set_px); }
static inline void render_icon_app_32(int x, int y, pixel_setter_fn set_px) { render_svg_app(x, y, 32, set_px); }
static inline void render_icon_file_32(int x, int y, pixel_setter_fn set_px) { render_svg_unknown(x, y, 32, set_px); }

#endif
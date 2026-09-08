#ifndef _VELO_ICONS_H
#define _VELO_ICONS_H

typedef void (*pixel_setter_fn)(int x, int y, unsigned int color);

#define SCALE_COORD(val, size) (((val) * (size)) / 32)

#define VELO_ICON_PC       1
#define VELO_ICON_FOLDER   2
#define VELO_ICON_DOC      3
#define VELO_ICON_UNKNOWN  4
#define VELO_ICON_APP      5

#define COLOR_VANTIX_BLUE   0x000284C7
#define COLOR_FOLDER_YELLOW 0x00FBBF24
#define COLOR_FRAME_GRAY    0x0078716C

static inline unsigned int blend_col(unsigned int fg, unsigned int bg, unsigned int a) {
    if (a >= 255) return fg;
    if (a == 0) return bg;
    unsigned int inv = 255 - a;
    unsigned int r = (((fg >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * inv) >> 8;
    unsigned int g = (((fg >> 8) & 0xFF) * a + ((bg >> 8) & 0xFF) * inv) >> 8;
    unsigned int b = ((fg & 0xFF) * a + (bg & 0xFF) * inv) >> 8;
    return (r << 16) | (g << 8) | b;
}

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
}

static inline void render_svg_folder(int x, int y, int size, pixel_setter_fn set_px) {
    if (!set_px || size <= 0) return;
    int x1 = x + SCALE_COORD(2, size), x2 = x + SCALE_COORD(30, size);
    int y1 = y + SCALE_COORD(6, size), y2 = y + SCALE_COORD(28, size);

    // Lasche oben links
    for (int cy = y + SCALE_COORD(3, size); cy < y1; cy++) {
        for (int cx = x1 + SCALE_COORD(2, size); cx <= x1 + SCALE_COORD(12, size); cx++) {
            set_px(cx, cy, 0x00D97706);
        }
    }

    // Ordner-Körper (Aero Yellow Gradient)
    for (int cy = y1; cy <= y2; cy++) {
        for (int cx = x1; cx <= x2; cx++) {
            if (cy == y1 || cy == y2 || cx == x1 || cx == x2) {
                set_px(cx, cy, 0x00B45309);
            } else {
                unsigned int col = (cy < y1 + SCALE_COORD(10, size)) ? 0x00FDE68A : 0x00F59E0B;
                set_px(cx, cy, col);
            }
        }
    }
}

static inline void render_svg_doc(int x, int y, int size, pixel_setter_fn set_px) {
    if (!set_px || size <= 0) return;
    int x1 = x + SCALE_COORD(5, size), x2 = x + SCALE_COORD(27, size);
    int y1 = y + SCALE_COORD(3, size), y2 = y + SCALE_COORD(29, size);
    int fold = SCALE_COORD(6, size);

    for (int cy = y1; cy <= y2; cy++) {
        for (int cx = x1; cx <= x2; cx++) {
            if (cx >= x2 - fold && cy <= y1 + fold && (cx - (x2 - fold)) > (fold - (cy - y1))) continue;
            unsigned int c = (cy == y1 || cy == y2 || cx == x1 || cx == x2) ? 0x0094A3B8 : 0x00FFFFFF;
            set_px(cx, cy, c);
        }
    }
    // Textzeilen im Dokument
    for (int line = 0; line < 3; line++) {
        int ly = y1 + SCALE_COORD(10 + line * 4, size);
        for (int lx = x1 + SCALE_COORD(4, size); lx <= x2 - SCALE_COORD(4, size); lx++) {
            set_px(lx, ly, 0x00CBD5E1);
        }
    }
}

static inline void render_svg_app(int x, int y, int size, pixel_setter_fn set_px) {
    if (!set_px || size <= 0) return;
    int x1 = x + SCALE_COORD(3, size), x2 = x + SCALE_COORD(29, size);
    int y1 = y + SCALE_COORD(3, size), y2 = y + SCALE_COORD(29, size);

    for (int cy = y1; cy <= y2; cy++) {
        for (int cx = x1; cx <= x2; cx++) {
            if (cy == y1 || cy == y2 || cx == x1 || cx == x2) {
                set_px(cx, cy, 0x000369A1);
            } else if (cy <= y1 + SCALE_COORD(7, size)) {
                set_px(cx, cy, 0x000284C7);
            } else {
                set_px(cx, cy, 0x00F8FAFC);
            }
        }
    }
    set_px(x1 + SCALE_COORD(3, size), y1 + SCALE_COORD(3, size), 0x00EF4444);
    set_px(x1 + SCALE_COORD(6, size), y1 + SCALE_COORD(3, size), 0x00F59E0B);
}

static inline void render_svg_unknown(int x, int y, int size, pixel_setter_fn set_px) {
    render_svg_doc(x, y, size, set_px);
}

static inline void render_icon_pc_32(int x, int y, pixel_setter_fn set_px) { render_icon_pc(x, y, 32, set_px); }
static inline void render_icon_folder_32(int x, int y, pixel_setter_fn set_px) { render_svg_folder(x, y, 32, set_px); }
static inline void render_icon_doc_32(int x, int y, pixel_setter_fn set_px) { render_svg_doc(x, y, 32, set_px); }
static inline void render_icon_app_32(int x, int y, pixel_setter_fn set_px) { render_svg_app(x, y, 32, set_px); }
static inline void render_icon_file_32(int x, int y, pixel_setter_fn set_px) { render_svg_unknown(x, y, 32, set_px); }

#endif
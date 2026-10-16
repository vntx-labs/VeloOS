#include <stddef.h>
#include <stdint.h>
#include "intro.h"
#include "font.h"
#include "wm.h"
#include "sched.h"
#include "kshell.h"

extern UINTN gop_width;
extern UINTN gop_height;
extern UINT32 *g_backbuffer;
extern int g_current_tty;
extern int g_desktop_alive;

void swap_buffers(void);
void clear_screen_graphics(UINT32 color);
void put_pixel(UINTN x, UINTN y, UINT32 color);
UINT32 get_pixel(UINTN x, UINTN y);
void draw_filled_rect(UINTN start_x, UINTN start_y, UINTN width, UINTN height, UINT32 color);

static void cinema_delay_ms(int ms) {
    for (volatile int i = 0; i < ms * 40000; i++) {
        __asm__ volatile("pause");
    }
}

static UINT32 g_rand_seed = 0x517CC1B7;
static inline UINT32 fast_rand(void) {
    g_rand_seed = g_rand_seed * 1664525U + 1013904223U;
    return (g_rand_seed >> 16) & 0xFFFF;
}

static unsigned int isqrt(unsigned int val) {
    unsigned int temp, g = 0;
    unsigned int b = 0x8000;
    while (b) {
        temp = g + b;
        if (temp * temp <= val) g = temp;
        b >>= 1;
    }
    return g;
}

static const int8_t g_sin_lut[256] = {
      0,   3,   6,   9,  12,  15,  18,  21,  24,  27,  30,  33,  36,  39,  42,  45,
     48,  51,  54,  57,  59,  62,  65,  67,  70,  73,  75,  78,  80,  82,  85,  87,
     89,  91,  94,  96,  98, 100, 102, 104, 106, 107, 109, 111, 112, 114, 115, 116,
    118, 119, 120, 121, 122, 123, 123, 124, 125, 125, 126, 126, 126, 126, 126, 126,
    126, 126, 126, 126, 126, 126, 126, 125, 125, 124, 123, 123, 122, 121, 120, 119,
    118, 116, 115, 114, 112, 111, 109, 107, 106, 104, 102, 100,  98,  96,  94,  91,
     89,  87,  85,  82,  80,  78,  75,  73,  70,  67,  65,  62,  59,  57,  54,  51,
     48,  45,  42,  39,  36,  33,  30,  27,  24,  21,  18,  15,  12,   9,   6,   3,
      0,  -3,  -6,  -9, -12, -15, -18, -21, -24, -27, -30, -33, -36, -39, -42, -45,
    -48, -51, -54, -57, -59, -62, -65, -67, -70, -73, -75, -78, -80, -82, -85, -87,
    -89, -91, -94, -96, -98,-100,-102,-104,-106,-107,-109,-111,-112,-114,-115,-116,
   -118,-119,-120,-121,-122,-123,-123,-124,-125,-125,-126,-126,-126,-126,-126,-126,
   -126,-126,-126,-126,-126,-126,-126,-125,-125,-124,-123,-123,-122,-121,-120,-119,
   -118,-116,-115,-114,-112,-111,-109,-107,-106,-104,-102,-100, -98, -96, -94, -91,
    -89, -87, -85, -82, -80, -78, -75, -73, -70, -67, -65, -62, -59, -57, -54, -51,
    -48, -45, -42, -39, -36, -33, -30, -27, -24, -21, -18, -15, -12,  -9,  -6,  -3
};

static inline int c_sin(int a) { return g_sin_lut[(uint8_t)a]; }
static inline int c_cos(int a) { return g_sin_lut[(uint8_t)(a + 64)]; }

static inline UINT32 add_glow(UINT32 base, UINT32 glow) {
    UINT32 rb = (base & 0x00FF00FF) + (glow & 0x00FF00FF);
    UINT32 g  = (base & 0x0000FF00) + (glow & 0x0000FF00);
    if (rb & 0xFF000000) rb = (rb & 0x00FFFFFF) | 0x00FF0000;
    if (rb & 0x0000FF00) rb = (rb & 0x00FF0000) | 0x000000FF;
    if (g  & 0x00FF0000) g  = 0x0000FF00;
    return (rb & 0x00FF00FF) | (g & 0x0000FF00);
}

static inline UINT32 blend_color(UINT32 fg, UINT32 bg, UINT32 alpha) {
    if (alpha >= 255) return fg;
    if (alpha == 0) return bg;
    UINT32 inv = 255 - alpha;
    UINT32 rb = (((fg & 0x00FF00FF) * alpha + (bg & 0x00FF00FF) * inv) >> 8) & 0x00FF00FF;
    UINT32 g  = (((fg & 0x0000FF00) * alpha + (bg & 0x0000FF00) * inv) >> 8) & 0x0000FF00;
    return rb | g;
}

/* Supernatural Season 9 Hitzegradient */
static UINT32 fire_palette_hd(int heat) {
    if (heat <= 0) return 0x00000000;
    if (heat > 255) heat = 255;

    if (heat < 40) {
        int r = (heat * 130) / 40;
        int g = (heat * 18) / 40;
        return ((UINT32)r << 16) | ((UINT32)g << 8);
    } else if (heat < 110) {
        int h = heat - 40;
        int r = 130 + (h * 125) / 70;
        int g = 18 + (h * 95) / 70;
        return ((UINT32)r << 16) | ((UINT32)g << 8) | 0x02;
    } else if (heat < 185) {
        int h = heat - 110;
        int r = 255;
        int g = 113 + (h * 125) / 75;
        int b = 4 + (h * 46) / 75;
        return ((UINT32)r << 16) | ((UINT32)g << 8) | (UINT32)b;
    } else {
        int h = heat - 185;
        int r = 255;
        int g = 238 + (h * 17) / 70;
        int b = 50 + (h * 205) / 70;
        return ((UINT32)r << 16) | ((UINT32)g << 8) | (UINT32)b;
    }
}

/* DYNAMISCHE PARTIKEL-FLAMMEN-STRUKTUR (128 Flammenzungen) */
#define NUM_TONGUES 128
typedef struct {
    uint8_t angle_base;
    uint8_t speed;
    uint8_t length_factor;
    uint8_t flicker_seed;
} FlameTongue;

static FlameTongue g_tongues[NUM_TONGUES];
static int g_tongues_init = 0;

static void init_tongues_if_needed(void) {
    if (g_tongues_init) return;
    for (int i = 0; i < NUM_TONGUES; i++) {
        g_tongues[i].angle_base = (uint8_t)(i * 2);
        g_tongues[i].speed = 2 + (uint8_t)(fast_rand() % 5);
        g_tongues[i].length_factor = 25 + (uint8_t)(fast_rand() % 45);
        g_tongues[i].flicker_seed = (uint8_t)fast_rand();
    }
    g_tongues_init = 1;
}

/*
 * DER GESTOCHEN SCHARFE VELO-STARTORB
 */
static void render_hd_velo_startorb(int cx, int cy, int radius, int frame) {
    if (radius <= 0) return;
    int w = (int)gop_width;
    int h = (int)gop_height;

    int r_sq = radius * radius;
    int halo_r = radius + (radius * 35) / 100;
    int halo_sq = halo_r * halo_r;

    for (int dy = -halo_r; dy <= halo_r; dy++) {
        int py = cy + dy;
        if (py < 0 || py >= h) continue;
        int dy2 = dy * dy;
        UINT32 *row = &g_backbuffer[py * w];

        for (int dx = -halo_r; dx <= halo_r; dx++) {
            int px = cx + dx;
            if (px < 0 || px >= w) continue;

            int dist_sq = dx * dx + dy2;

            if (dist_sq <= r_sq) {
                int z = (int)isqrt((unsigned int)(r_sq - dist_sq));
                int diffuse = ((dx * -90) + (dy * -110) + (z * 190)) / radius;
                if (diffuse < 0) diffuse = 0;
                if (diffuse > 255) diffuse = 255;

                int cr = (diffuse * 15) / 255;
                int cg = 40 + (diffuse * 140) / 255;
                int cb = 110 + (diffuse * 145) / 255;
                UINT32 orb_col = ((UINT32)cr << 16) | ((UINT32)cg << 8) | (UINT32)cb;

                if (dist_sq > (radius - 4) * (radius - 4)) {
                    orb_col = add_glow(orb_col, 0x00FBBF24);
                }

                row[px] = blend_color(orb_col, row[px], 245);
            } else if (dist_sq <= halo_sq) {
                int falloff = 255 - ((dist_sq - r_sq) * 255) / (halo_sq - r_sq);
                if (falloff > 0) {
                    UINT32 glow = blend_color(0x00F59E0B, 0x000284C7, (UINT32)falloff);
                    row[px] = add_glow(row[px], blend_color(glow, 0x00000000, (UINT32)(falloff / 2)));
                }
            }
        }
    }

    int v_size = (radius * 60) / 100;
    int pulse = 210 + (c_sin((frame * 20) & 0xFF) * 45) / 128;
    UINT32 gold_trim = blend_color(0x005AC0E0, 0x00FDE68A, (UINT32)pulse);
    UINT32 white_core = 0x00FFFFFF;

    draw_line_aa(cx - v_size, cy - v_size + 3, cx - v_size / 4, cy + v_size, 4, gold_trim);
    draw_line_aa(cx - v_size / 4, cy + v_size, cx + v_size - 3, cy - v_size, 4, gold_trim);
    draw_line_aa(cx - v_size - 3, cy + 2, cx + v_size + 4, cy - 2, 3, 0x00F59E0B);

    draw_line_aa(cx - v_size, cy - v_size + 3, cx - v_size / 4, cy + v_size, 2, white_core);
    draw_line_aa(cx - v_size / 4, cy + v_size, cx + v_size - 3, cy - v_size, 2, white_core);
    draw_line_aa(cx - v_size - 1, cy + 2, cx + v_size + 2, cy - 2, 1, white_core);
}

/*
 * FLÜSSIGES, ZÜNGELNDES FEUER- & IRIS-PLASMA
 */
static void render_living_fire_ring(int cx, int cy, int orb_radius, int frame, int zoom) {
    int w = (int)gop_width;
    int h = (int)gop_height;

    init_tongues_if_needed();

    int iris_inner = orb_radius + 4;
    int base_outer = 150 + (zoom * 3) / 2;

    // 1. Chaotisch züngelnde Flammenzungen (128 Eruptionsherde)
    for (int i = 0; i < NUM_TONGUES; i++) {
        // Kontinuierliche Winkel-Drift
        int cur_angle = (g_tongues[i].angle_base + (frame * g_tongues[i].speed) / 2) & 0xFF;
        
        // Zufälliges Züngeln & Verlängern/Verkürzen der Zungen
        int wave = c_sin((cur_angle * 3 + frame * 16 + g_tongues[i].flicker_seed) & 0xFF);
        int tongue_len = base_outer + (wave * g_tongues[i].length_factor) / 128;
        if (tongue_len < iris_inner) tongue_len = iris_inner;

        int s = c_sin(cur_angle);
        int c = c_cos(cur_angle);

        // Zeichne den Fächer der Zunge
        for (int r = iris_inner; r < tongue_len; r += 2) {
            int px = cx + (c * r) / 128;
            int py = cy + (s * r) / 128;

            if (px >= 0 && px < w && py >= 0 && py < h) {
                int heat = 255 - ((r - iris_inner) * 255) / (tongue_len - iris_inner + 25);
                if (heat < 0) heat = 0;

                UINT32 col = fire_palette_hd(heat);
                UINT32 under = get_pixel((UINTN)px, (UINTN)py);
                put_pixel((UINTN)px, (UINTN)py, add_glow(under, col));
            }
        }
    }

    // 2. Dichte Hintergrund-Aura (Interpoliert die Lücken zwischen Zungen)
    for (int deg = 0; deg < 512; deg++) {
        int angle = deg / 2;
        int s = c_sin(angle);
        int c = c_cos(angle);

        int jitter = (c_sin((deg * 4 + frame * 22) & 0xFF) * 18) / 128;
        int r_end = iris_inner + 45 + jitter;

        for (int r = iris_inner; r < r_end; r += 2) {
            int px = cx + (c * r) / 128;
            int py = cy + (s * r) / 128;
            if (px >= 0 && px < w && py >= 0 && py < h) {
                int heat = 255 - ((r - iris_inner) * 255) / (r_end - iris_inner + 10);
                UINT32 col = fire_palette_hd(heat / 2);
                put_pixel((UINTN)px, (UINTN)py, add_glow(get_pixel((UINTN)px, (UINTN)py), col));
            }
        }
    }

    // 3. Innere Iris-Fasern (Zucken und pulsieren wie das Supernatural-Auge)
    for (int deg = 0; deg < 512; deg++) {
        int angle = deg / 2;
        int s = c_sin(angle);
        int c = c_cos(angle);

        int twitch = (c_cos((deg * 6 + frame * 30) & 0xFF) * 12) / 128;
        int eye_rim = orb_radius + 12 + twitch;

        for (int r = orb_radius; r < eye_rim; r++) {
            int px = cx + (c * r) / 128;
            int py = cy + (s * r) / 128;
            if (px >= 0 && px < w && py >= 0 && py < h) {
                put_pixel((UINTN)px, (UINTN)py, 0x00FFFBEB);
            }
        }
    }
}

/* Anamorpher Lens Flare mit Shutter-Flicker */
static void render_hd_lens_flare(int cx, int cy, int flare_power) {
    if (flare_power <= 0) return;
    int w = (int)gop_width;
    int h = (int)gop_height;

    int flare_span = (w * 94) / 100;
    int flare_h = 3 + (flare_power * 4) / 255;

    for (int fy = -flare_h * 4; fy <= flare_h * 4; fy++) {
        int py = cy + fy;
        if (py < 0 || py >= h) continue;

        int alpha_y = 255 - (isqrt((unsigned int)(fy * fy)) * 255) / (flare_h * 4 + 1);
        alpha_y = (alpha_y * flare_power) / 255;

        UINT32 gold_flare = blend_color(0x00F59E0B, 0x00000000, (UINT32)alpha_y);

        for (int fx = -flare_span / 2; fx < flare_span / 2; fx += 2) {
            int px = cx + fx;
            if (px < 0 || px >= w) continue;
            int alpha_x = 255 - (isqrt((unsigned int)(fx * fx)) * 255) / (flare_span / 2 + 1);
            UINT32 col = blend_color(gold_flare, 0x00000000, (UINT32)alpha_x);
            put_pixel((UINTN)px, (UINTN)py, add_glow(get_pixel((UINTN)px, (UINTN)py), col));
        }
    }

    draw_filled_rect((UINTN)(cx - flare_span / 2), (UINTN)(cy - flare_h / 2), (UINTN)flare_span, (UINTN)flare_h, 0x00FFFFFF);
}

/* Frame Compositing */
static void render_living_supernatural_frame(int frame, int zoom_px, int flare_power) {
    clear_screen_graphics(0x00000000);

    int cx = (int)gop_width / 2;
    int cy = (int)gop_height / 2;
    int orb_radius = 72 + zoom_px;

    // 1. Züngelnder Flammen- & Irisring
    render_living_fire_ring(cx, cy, orb_radius, frame, zoom_px);

    // 2. Unser 3D-Velo Startorb
    render_hd_velo_startorb(cx, cy, orb_radius, frame);

    // 3. Anamorphic Lens Flare
    render_hd_lens_flare(cx, cy, flare_power);

    // 4. Typografie "VELOOS"
    const char *title = "V E L O O S";
    int tx = cx - (11 * 16) / 2;
    int ty = cy - 14;

    wm_draw_text_scaled(title, tx - 2, ty, 2, 0x0092400E, 0x00000000);
    wm_draw_text_scaled(title, tx + 2, ty, 2, 0x0092400E, 0x00000000);
    wm_draw_text_scaled(title, tx, ty - 2, 2, 0x00F59E0B, 0x00000000);
    wm_draw_text_scaled(title, tx, ty + 2, 2, 0x00F59E0B, 0x00000000);
    wm_draw_text_scaled(title, tx, ty, 2, 0x00FFFFFF, 0x00000000);
}

/* =========================================================================
 * DER ABLAUF: ECHTE CINEMATISCHE DYNAMIK
 * ========================================================================= */
void intro_play_glitch_sequence(void) {
    int cx = (int)gop_width / 2;
    int cy = (int)gop_height / 2;

    // ----------------------------------------------------
    // 1. BUSTED SOLAR FLASH (00:00)
    // ----------------------------------------------------
    clear_screen_graphics(0x00FFFFFF);
    swap_buffers();
    cinema_delay_ms(30);

    for (int ring = 420; ring >= 75; ring -= 40) {
        clear_screen_graphics(0x00000000);
        draw_circle_aa(cx, cy, ring, 0x00FFFFFF);
        draw_circle_aa(cx, cy, ring + 20, 0x00F59E0B);
        swap_buffers();
        cinema_delay_ms(10);
    }

    // ----------------------------------------------------
    // 2. DAS ZÜNGELNDE FEUERAUGE (00:01 - 00:04)
    // ----------------------------------------------------
    int total_frames = 55; // Flüssige 2.5 Sekunden
    for (int f = 0; f < total_frames; f++) {
        int zoom = (f * 24) / total_frames;
        int flare = 170 + (c_sin((f * 18) & 0xFF) * 85) / 128;

        render_living_supernatural_frame(f, zoom, flare);
        swap_buffers();
        cinema_delay_ms(22);
    }

    // ----------------------------------------------------
    // 3. BURNOUT-FLASH (00:05)
    // ----------------------------------------------------
    for (int burst = 0; burst <= 255; burst += 50) {
        clear_screen_graphics(blend_color(0x00FFFFFF, 0x00F59E0B, (UINT32)burst));
        swap_buffers();
        cinema_delay_ms(10);
    }

    // ----------------------------------------------------
    // 4. HARD-CUT AUF SCHWARZ (00:06)
    // ----------------------------------------------------
    clear_screen_graphics(0x00000000);
    swap_buffers();
    cinema_delay_ms(40);

    // 5. NAHTLOSER ÜBERGANG IN DEN DESKTOP
    UINT32 desktop_anthrazit = 0x000F172A;
    for (int f = 0; f <= 255; f += 40) {
        clear_screen_graphics(blend_color(desktop_anthrazit, 0x00000000, (UINT32)f));
        swap_buffers();
        cinema_delay_ms(12);
    }

    clear_screen_graphics(desktop_anthrazit);
    swap_buffers();
}

/* =========================================================================
 * SCHATTEN-TTY TRIGGER
 * ========================================================================= */
void intro_trigger_easter_egg(int return_tty) {
    int prev = (return_tty >= 1 && return_tty <= 8) ? return_tty : g_current_tty;
    g_current_tty = 8;
    
    clear_screen_graphics(0x00000000);
    swap_buffers();

    intro_play_glitch_sequence();

    clear_screen_graphics(0x000F172A);
    swap_buffers();
    kshell_start(8);
    kshell_mark_dirty();

    g_current_tty = prev;
    if (prev == 1 && g_desktop_alive) {
        wm_mark_all_dirty();
    } else {
        kshell_mark_dirty();
        kshell_tick_frame(prev);
    }
}
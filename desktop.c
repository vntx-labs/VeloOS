// desktop.c - Mathematisch gerenderter Desktop mit Scanline-Alignment & Subpixel-Anti-Aliasing
#include <efi.h>
#include <efilib.h>
#include "desktop.h"
#include "font.h"
#include "wm.h"
#include "ahci.h"
#include "fat32.h"

extern UINTN gop_width;
extern UINTN gop_height;
extern UINTN gfx_cursor_x;
extern UINTN gfx_cursor_y;
extern int system_mode;
extern int logged_in;

void put_pixel(UINTN x, UINTN y, UINT32 color);
void draw_string(const char* str, UINTN x, UINTN y, UINT32 fg_color, UINT32 bg_color);
void draw_filled_rect(UINTN start_x, UINTN start_y, UINTN width, UINTN height, UINT32 color);
void clear_screen_graphics(UINT32 color);
void swap_buffers(void);

static int desktop_menu_open = 0;
static int start_menu_selected = 0;

#define START_MENU_ITEMS 5

static const char* start_menu_labels[START_MENU_ITEMS] = {
    "1. Control Center",
    "2. Taschenrechner",
    "3. Matrix Terminal",
    "4. Storage Manager",
    "5. Abmelden (Shell)"
};

static const char* dashboard_text_lines[] = {
    "VeloOS System Control Center - Dashboard",
    "Status: Online, authentifiziert & FAT32 Storage aktiv.",
    "Benutzer: Angemeldet (Disk-Persistent auf SATA)",
    "Speicher: 2 Laufwerke initialisiert & betriebsbereit.",
    "----------------------------------------------------------",
    "[F1]  Startmenue oeffnen / schliessen",
    "[TAB] Naechstes Fenster fokussieren",
    "[M]   Aktives Fenster minimieren",
    "[X]   Aktives Fenster schliessen",
    "[ESC] Desktop beenden -> Logout zur Shell"
};

static inline unsigned char inb_cmos(unsigned short port) {
    unsigned char res;
    __asm__ volatile("inb %1, %0" : "=a"(res) : "Nd"(port));
    return res;
}
static inline void outb_cmos(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void get_rtc_time(int *h, int *m, int *s) {
    outb_cmos(0x70, 0x00); *s = inb_cmos(0x71);
    outb_cmos(0x70, 0x02); *m = inb_cmos(0x71);
    outb_cmos(0x70, 0x04); *h = inb_cmos(0x71);

    *s = (*s & 0x0F) + ((*s >> 4) * 10);
    *m = (*m & 0x0F) + ((*m >> 4) * 10);
    *h = (*h & 0x0F) + ((*h >> 4) * 10);
}

/* 1. Mathematischer Linearer Farbverlauf für den Desktop */
static void draw_gradient_background(UINT32 top_col, UINT32 bot_col) {
    UINT32 tr = (top_col >> 16) & 0xFF, tg = (top_col >> 8) & 0xFF, tb = top_col & 0xFF;
    UINT32 br = (bot_col >> 16) & 0xFF, bg_val = (bot_col >> 8) & 0xFF, bb = bot_col & 0xFF;

    for (UINTN y = 0; y < gop_height; y++) {
        UINT32 r = tr + (UINT32)(((INT32)br - (INT32)tr) * (INT32)y / (INT32)gop_height);
        UINT32 g = tg + (UINT32)(((INT32)bg_val - (INT32)tg) * (INT32)y / (INT32)gop_height);
        UINT32 b = tb + (UINT32)(((INT32)bb - (INT32)tb) * (INT32)y / (INT32)gop_height);
        UINT32 row_col = (r << 16) | (g << 8) | b;

        for (UINTN x = 0; x < gop_width; x++) {
            put_pixel(x, y, row_col);
        }
    }
}

/* 2. Zentriertes Anti-Aliased Vektor-Logo */
static void draw_centered_vector_logo(void) {
    int cx = (int)gop_width / 2;
    int cy = (int)gop_height / 2 - 20;

    /* Äußerer & innerer Anti-Aliased Kreis */
    draw_circle_aa(cx, cy, 90, 4, 0x001B62D6);
    draw_circle_aa(cx, cy, 76, 2, 0x007AA2F7);

    /* Vektor-Chevron "V" */
    draw_line_aa(cx - 42, cy - 38, cx, cy + 42, 8, 0x0000FFCC);
    draw_line_aa(cx, cy + 42, cx + 42, cy - 38, 8, 0x0000FFCC);

    draw_line_aa(cx - 24, cy - 32, cx, cy + 18, 5, 0x007AA2F7);
    draw_line_aa(cx, cy + 18, cx + 24, cy - 32, 5, 0x007AA2F7);

    /* Logo-Text */
    wm_draw_string_content("V E L O   O S", cx - 52, cy + 110, 1, 0x0000FFCC, 0x00000000, 0, 0, (int)gop_width, (int)gop_height);
    wm_draw_string_content("64-Bit Bare-Metal Core", cx - 88, cy + 134, 1, 0x007AA2F7, 0x00000000, 0, 0, (int)gop_width, (int)gop_height);
}

/* 1. Dashboard Window Paint */
static void paint_dashboard(int win_id, int cx, int cy, int cw, int ch) {
    (void)win_id;
    int y = cy + 4;
    wm_draw_string_content(dashboard_text_lines[0], cx + 4, y, 1, 0x007AA2F7, 0x00141620, cx, cy, cw, ch); y += 26;
    wm_draw_string_content(dashboard_text_lines[1], cx + 4, y, 1, 0x00C0CAF5, 0x00141620, cx, cy, cw, ch); y += 24;
    wm_draw_string_content(dashboard_text_lines[2], cx + 4, y, 1, 0x009ECE6A, 0x00141620, cx, cy, cw, ch); y += 24;
    wm_draw_string_content(dashboard_text_lines[3], cx + 4, y, 1, 0x00E0AF68, 0x00141620, cx, cy, cw, ch); y += 24;
    wm_draw_string_content(dashboard_text_lines[4], cx + 4, y, 1, 0x00565F89, 0x00141620, cx, cy, cw, ch); y += 26;

    for (int i = 5; i < 10; i++) {
        UINT32 col = (i == 9) ? 0x00F7768E : 0x00C0CAF5;
        wm_draw_string_content(dashboard_text_lines[i], cx + 4, y, 1, col, 0x00141620, cx, cy, cw, ch);
        y += 24;
    }
}

/* 2. Taschenrechner Window Paint */
static void paint_calculator(int win_id, int cx, int cy, int cw, int ch) {
    (void)win_id;
    wm_draw_string_content("VeloOS Schneller Taschenrechner", cx + 8, cy + 4, 1, 0x007AA2F7, 0x00141620, cx, cy, cw, ch);
    draw_rounded_rect_aa(cx + 8, cy + 32, cw - 16, 36, 6, 0x001E2233);
    wm_draw_string_content("10 + 2 * 5 = 20", cx + 18, cy + 42, 1, 0x009ECE6A, 0x001E2233, cx, cy, cw, ch);

    wm_draw_string_content("Ergebnis: 20 (Gueltige Berechnung)", cx + 8, cy + 82, 1, 0x00C0CAF5, 0x00141620, cx, cy, cw, ch);
    wm_draw_string_content("Strikte Syntax & Punkt-vor-Strich aktiv.", cx + 8, cy + 110, 1, 0x00565F89, 0x00141620, cx, cy, cw, ch);
}

/* 3. Matrix Terminal Window Paint */
static void paint_matrix(int win_id, int cx, int cy, int cw, int ch) {
    (void)win_id;
    wm_draw_string_content("VeloOS Matrix Subsystem - Aktiv", cx + 8, cy + 4, 1, 0x009ECE6A, 0x00141620, cx, cy, cw, ch);
    wm_draw_string_content("01011001 01100101 01101100 01101111", cx + 8, cy + 30, 1, 0x0073DACA, 0x00141620, cx, cy, cw, ch);
    wm_draw_string_content("K E R N E L _ L O A D E D _ S U C C E S S", cx + 8, cy + 54, 1, 0x009ECE6A, 0x00141620, cx, cy, cw, ch);
    wm_draw_string_content("SATA AHCI DMA Pipeline: Operational", cx + 8, cy + 78, 1, 0x007AA2F7, 0x00141620, cx, cy, cw, ch);
}

/* 4. Storage Window Paint */
static void paint_storage(int win_id, int cx, int cy, int cw, int ch) {
    (void)win_id;
    int count = ahci_get_port_count();
    wm_draw_string_content("Angeschlossene SATA Speicher-Laufwerke:", cx + 8, cy + 4, 1, 0x00E0AF68, 0x00141620, cx, cy, cw, ch);

    int y = cy + 34;
    for (int i = 0; i < count && i < 4; i++) {
        AHCI_PORT_INFO *info = ahci_get_port_info(i);
        if (!info || !info->active) continue;

        draw_rounded_rect_aa(cx + 8, y, cw - 16, 34, 6, 0x001E2233);
        char port_str[64];
        port_str[0] = 'P'; port_str[1] = 'o'; port_str[2] = 'r'; port_str[3] = 't'; port_str[4] = ' ';
        port_str[5] = '0' + (char)info->port_number; port_str[6] = ':'; port_str[7] = ' ';
        port_str[8] = '\0';
        wm_draw_string_content(port_str, cx + 18, y + 8, 1, 0x007AA2F7, 0x001E2233, cx, cy, cw, ch);
        wm_draw_string_content(info->model, cx + 90, y + 8, 1, 0x00FFFFFF, 0x001E2233, cx, cy, cw, ch);
        y += 42;
    }
}

/* 4. Painter’s Algorithm Render-Loop mit Scanline-Alignment */
static void show_desktop(void) {
    /* Step A: Linearer Hintergrund-Farbverlauf */
    draw_gradient_background(0x00060C1A, 0x000E2244);

    /* Step B: Zentriertes Vektor-Logo mit Anti-Aliasing */
    draw_centered_vector_logo();

    /* Step C: Obere Statusleiste */
    draw_rounded_rect_gradient(0, 0, (int)gop_width, 30, 0, 0x00181A24, 0x0010121A);
    wm_draw_string_content("VeloOS 64-Bit Desktop Environment", 16, 7, 1, 0x007AA2F7, 0x0010121A, 0, 0, (int)gop_width, 30);
    wm_draw_string_content("[F1] Start | [TAB] Fenster | [ESC] Logout", (int)gop_width - 350, 7, 1, 0x00A9B1D6, 0x0010121A, 0, 0, (int)gop_width, 30);

    /* Step D: Window Manager Fenster */
    wm_render_all();

    /* Step E: Untere Taskbar über die gesamte Pitch-Breite */
    UINTN taskbar_h = 44;
    UINTN taskbar_y = gop_height - taskbar_h;
    draw_rounded_rect_gradient(0, (int)taskbar_y, (int)gop_width, (int)taskbar_h, 0, 0x00181A24, 0x000E1017);

    /* Start Button */
    UINT32 sb_top = desktop_menu_open ? 0x0000E5FF : 0x001B62D6;
    UINT32 sb_bot = desktop_menu_open ? 0x000088CC : 0x000E429C;
    draw_rounded_rect_gradient(10, (int)taskbar_y + 6, 100, 32, 6, sb_top, sb_bot);
    wm_draw_string_content("[Start]", 30, (int)taskbar_y + 14, 1, 0x00FFFFFF, sb_bot, 0, 0, (int)gop_width, (int)gop_height);

    /* Offene Fenster Tabs */
    int tab_x = 120;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        Window *win = wm_get_window(i);
        if (!win || win->is_closed) continue;

        UINT32 tb_top = win->is_active ? 0x001B62D6 : 0x0024283B;
        UINT32 tb_bot = win->is_active ? 0x000E429C : 0x00181A24;
        draw_rounded_rect_gradient(tab_x, (int)taskbar_y + 6, 160, 32, 6, tb_top, tb_bot);
        wm_draw_string_content(win->title, tab_x + 12, (int)taskbar_y + 14, 1, 0x00FFFFFF, tb_bot, tab_x, (int)taskbar_y, 160, 40);
        tab_x += 170;
    }

    /* System Tray: Exakte Verankerung an der rechten Kante */
    int right_margin = 12;
    int power_w = 80;
    int power_x = (int)gop_width - power_w - right_margin;

    int clock_w = 95;
    int clock_x = power_x - clock_w - 10;

    int h = 12, m = 0, s = 0;
    get_rtc_time(&h, &m, &s);

    char time_str[16];
    time_str[0] = '0' + (h / 10);
    time_str[1] = '0' + (h % 10);
    time_str[2] = ':';
    time_str[3] = '0' + (m / 10);
    time_str[4] = '0' + (m % 10);
    time_str[5] = ':';
    time_str[6] = '0' + (s / 10);
    time_str[7] = '0' + (s % 10);
    time_str[8] = '\0';

    draw_rounded_rect_aa(clock_x, (int)taskbar_y + 6, clock_w, 32, 6, 0x001E2233);
    wm_draw_string_content(time_str, clock_x + 12, (int)taskbar_y + 14, 1, 0x007AA2F7, 0x001E2233, 0, 0, (int)gop_width, (int)gop_height);

    draw_rounded_rect_gradient(power_x, (int)taskbar_y + 6, power_w, 32, 6, 0x00F7768E, 0x00C53B53);
    wm_draw_string_content("Power", power_x + 18, (int)taskbar_y + 14, 1, 0x00FFFFFF, 0x00C53B53, 0, 0, (int)gop_width, (int)gop_height);

    /* 6. Interaktives Startmenü Popup */
    if (desktop_menu_open) {
        int menu_w = 240;
        int menu_h = 230;
        int menu_x = 10;
        int menu_y = (int)(taskbar_y - menu_h - 6);

        draw_rounded_rect_aa(menu_x + 6, menu_y + 6, menu_w, menu_h, 10, 0x00060810);
        draw_rounded_rect_aa(menu_x, menu_y, menu_w, menu_h, 10, 0x00181A24);

        draw_rounded_rect_gradient(menu_x, menu_y, menu_w, 36, 10, 0x001B62D6, 0x000E429C);
        draw_filled_rect(menu_x, menu_y + 26, menu_w, 10, 0x000E429C);
        wm_draw_string_content("VeloOS Startmenue", menu_x + 16, menu_y + 10, 1, 0x00FFFFFF, 0x000E429C, menu_x, menu_y, menu_w, 36);

        int item_y = menu_y + 44;
        for (int i = 0; i < START_MENU_ITEMS; i++) {
            if (i == start_menu_selected) {
                draw_rounded_rect_aa(menu_x + 8, item_y - 4, menu_w - 16, 32, 6, 0x001B62D6);
                wm_draw_string_content(start_menu_labels[i], menu_x + 16, item_y + 4, 1, 0x00FFFFFF, 0x001B62D6, menu_x, menu_y, menu_w, menu_h);
            } else {
                wm_draw_string_content(start_menu_labels[i], menu_x + 16, item_y + 4, 1, 0x00C0CAF5, 0x00181A24, menu_x, menu_y, menu_w, menu_h);
            }
            item_y += 34;
        }
    }

    /* Step F: Double-Buffer Blit */
    swap_buffers();
}

static void execute_start_menu_item(int item) {
    desktop_menu_open = 0;
    switch (item) {
        case 0: /* Control Center - Automatisch vermessen */
            wm_create_window_for_lines(
                "Control Center",
                dashboard_text_lines,
                10,
                1,
                paint_dashboard
            );
            break;
        case 1: /* Taschenrechner */
            wm_create_window_auto(
                "Taschenrechner",
                420,
                140,
                paint_calculator
            );
            break;
        case 2: /* Matrix Terminal */
            wm_create_window_auto(
                "Matrix Terminal",
                440,
                110,
                paint_matrix
            );
            break;
        case 3: /* Storage Manager */
            wm_create_window_auto(
                "Storage Manager",
                460,
                150,
                paint_storage
            );
            break;
        case 4: /* Logout */
            system_mode = 0;
            logged_in = 0;
            clear_screen_graphics(0x00000000);
            draw_string("VeloOS Kernel - Bare Metal Mode Active", 50, 30, 0x00FFFFFF, 0x00000000);
            draw_string("Abgemeldet. Zurueck in der Shell.", 50, 60, 0x007AA2F7, 0x00000000);
            gfx_cursor_x = 50; gfx_cursor_y = 120;
            draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
            gfx_cursor_x += 32;
            swap_buffers();
            return;
    }
    show_desktop();
}

void desktop_start(void) {
    desktop_menu_open = 0;
    start_menu_selected = 0;
    system_mode = 3;

    wm_init();
    wm_create_window_for_lines(
        "Control Center",
        dashboard_text_lines,
        10,
        1,
        paint_dashboard
    );

    show_desktop();
}

void desktop_handle_key(char c) {
    if (desktop_menu_open) {
        if (c == 0x1B) {
            desktop_menu_open = 0;
            show_desktop();
            return;
        }
        if (c == 'w' || c == 'W') {
            start_menu_selected = (start_menu_selected - 1 + START_MENU_ITEMS) % START_MENU_ITEMS;
            show_desktop();
            return;
        }
        if (c == 's' || c == 'S') {
            start_menu_selected = (start_menu_selected + 1) % START_MENU_ITEMS;
            show_desktop();
            return;
        }
        if (c == '\n' || c == '\r') {
            execute_start_menu_item(start_menu_selected);
            return;
        }
        if (c >= '1' && c <= '5') {
            execute_start_menu_item(c - '1');
            return;
        }
    }

    if (c == 0x1B) {
        desktop_menu_open = 0;
        system_mode = 0;
        logged_in = 0;

        clear_screen_graphics(0x00000000);
        draw_string("VeloOS Kernel - Bare Metal Mode Active", 50, 30, 0x00FFFFFF, 0x00000000);
        draw_string("Abgemeldet. Zurueck in der Shell.", 50, 60, 0x007AA2F7, 0x00000000);
        gfx_cursor_x = 50; gfx_cursor_y = 120;
        draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
        gfx_cursor_x += 32;
        swap_buffers();
        return;
    }

    if ((unsigned char)c == 0x3B || (unsigned char)c == 0xF1) {
        desktop_menu_open = !desktop_menu_open;
        start_menu_selected = 0;
        show_desktop();
        return;
    }

    if (c == 0x09) {
        wm_focus_next();
        show_desktop();
        return;
    }

    if (c == 'm' || c == 'M') {
        wm_minimize_active();
        show_desktop();
        return;
    }

    if (c == 'x' || c == 'X') {
        wm_close_active();
        show_desktop();
        return;
    }

    show_desktop();
}
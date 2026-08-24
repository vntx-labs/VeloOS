// desktop.c - VeloOS Desktop mit automatischer Datumserkennung & MESZ/Sommerzeit-Berechnung
#include <efi.h>
#include <efilib.h>
#include "desktop.h"
#include "font.h"
#include "wm.h"
#include "ahci.h"
#include "fat32.h"
#include "mouse.h"
#include "calc.h"
#include "net.h"

extern UINTN gop_width;
extern UINTN gop_height;
extern UINTN gfx_cursor_x;
extern UINTN gfx_cursor_y;
extern int system_mode;
extern int logged_in;
extern UINT32 *g_backbuffer;
extern UINT32 *g_wall_buffer;

void put_pixel(UINTN x, UINTN y, UINT32 color);
void draw_filled_rect(UINTN start_x, UINTN start_y, UINTN width, UINTN height, UINT32 color);
void draw_string(const char* str, UINTN x, UINTN y, UINT32 fg_color, UINT32 bg_color);
void clear_screen_graphics(UINT32 color);
void swap_buffers(void);
void swap_buffers_rect(int rx, int ry, int rw, int rh);
void system_shutdown(void);
void system_reboot(void);

static int desktop_menu_open = 0;
static int start_menu_selected = 0;
static int last_second = -1;
static int last_synced_state = 0;
static int last_mouse_x = -1;
static int last_mouse_y = -1;

#define MAX_DISK_APPS 24
static char disk_apps[MAX_DISK_APPS][32];
static int disk_app_count = 0;

static UINT32 g_wall_src_w = 0;
static UINT32 g_wall_src_h = 0;
static int g_wall_loaded = 0;

static inline unsigned char inb_cmos(unsigned short port) {
    unsigned char res;
    __asm__ volatile("inb %1, %0" : "=a"(res) : "Nd"(port));
    return res;
}
static inline void outb_cmos(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* Liest Uhrzeit und volles Datum aus dem CMOS RTC */
static void get_rtc_full_datetime(int *year, int *month, int *day, int *h, int *m, int *s) {
    outb_cmos(0x70, 0x00); *s = inb_cmos(0x71);
    outb_cmos(0x70, 0x02); *m = inb_cmos(0x71);
    outb_cmos(0x70, 0x04); *h = inb_cmos(0x71);
    outb_cmos(0x70, 0x07); *day = inb_cmos(0x71);
    outb_cmos(0x70, 0x08); *month = inb_cmos(0x71);
    outb_cmos(0x70, 0x09); *year = inb_cmos(0x71);

    // BCD zu Binär konvertieren
    *s = (*s & 0x0F) + ((*s >> 4) * 10);
    *m = (*m & 0x0F) + ((*m >> 4) * 10);
    *h = (*h & 0x0F) + ((*h >> 4) * 10);
    *day = (*day & 0x0F) + ((*day >> 4) * 10);
    *month = (*month & 0x0F) + ((*month >> 4) * 10);
    *year = (*year & 0x0F) + ((*year >> 4) * 10);
    if (*year < 100) *year += 2000;
}

/*
 * Berechnet nach offizieller EU-Richtlinie, ob am gegebenen Datum Sommerzeit (MESZ) gilt
 */
static int is_european_summer_time(int year, int month, int day, int utc_hour) {
    if (month < 3 || month > 10) return 0; // Nov, Dez, Jan, Feb -> Normal-/Winterzeit
    if (month > 3 && month < 10) return 1; // Apr, Mai, Jun, Jul, Aug, Sep -> Immer Sommerzeit

    // Sakamoto-Algorithmus zur Wochentagsberechnung des 31. März bzw. 31. Oktober
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int y = year;
    if (month < 3) y--;
    int day_of_31 = (y + y/4 - y/100 + y/400 + t[month - 1] + 31) % 7;
    int last_sunday = 31 - day_of_31;

    if (month == 3) { // März: Umschaltung am letzten Sonntag um 01:00 UTC
        if (day > last_sunday) return 1;
        if (day == last_sunday && utc_hour >= 1) return 1;
        return 0;
    } else { // Oktober: Umschaltung am letzten Sonntag um 01:00 UTC
        if (day < last_sunday) return 1;
        if (day == last_sunday && utc_hour < 1) return 1;
        return 0;
    }
}

static int str_equal_nocase(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a >= 'a' && *a <= 'z' ? *a - ('a' - 'A') : *a;
        char cb = *b >= 'a' && *b <= 'z' ? *b - ('a' - 'A') : *b;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void scan_disk_applications(void) {
    disk_app_count = 0;
    int port_count = ahci_get_port_count();
    char raw_files[MAX_DISK_APPS][32];

    for (int p = 0; p < port_count && disk_app_count < MAX_DISK_APPS; p++) {
        AHCI_PORT_INFO *info = ahci_get_port_info(p);
        if (!info || !info->active) continue;

        int count = fat32_list_root(info->port_addr, raw_files, MAX_DISK_APPS);
        for (int i = 0; i < count && disk_app_count < MAX_DISK_APPS; i++) {
            if (str_equal_nocase(raw_files[i], "WALL.BIN") ||
                str_equal_nocase(raw_files[i], "ACCOUNT.DAT") ||
                str_equal_nocase(raw_files[i], "BOOTX64.EFI") ||
                str_equal_nocase(raw_files[i], "KERNEL.EFI")) {
                continue;
            }

            int len = 0;
            while (raw_files[i][len] != '\0') len++;

            if (len >= 4) {
                const char *ext = &raw_files[i][len - 4];
                if ((ext[0] == '.' && (ext[1] == 'B' || ext[1] == 'b') && (ext[2] == 'I' || ext[2] == 'i') && (ext[3] == 'N' || ext[3] == 'n')) ||
                    (ext[0] == '.' && (ext[1] == 'A' || ext[1] == 'a') && (ext[2] == 'P' || ext[2] == 'p') && (ext[3] == 'P' || ext[3] == 'p'))) {
                    
                    int c = 0;
                    while (raw_files[i][c] && c < 31) {
                        disk_apps[disk_app_count][c] = raw_files[i][c];
                        c++;
                    }
                    disk_apps[disk_app_count][c] = '\0';
                    disk_app_count++;
                }
            }
        }
    }
}

static void load_wallpaper_from_disk(void) {
    if (g_wall_loaded || g_wall_buffer == NULL) return;

    int port_count = ahci_get_port_count();
    for (int p = 0; p < port_count; p++) {
        AHCI_PORT_INFO *info = ahci_get_port_info(p);
        if (!info || !info->active) continue;

        UINT32 file_header[2];
        int read_hdr = fat32_read_file(info->port_addr, "WALL.BIN", file_header, 8);
        if (read_hdr >= 8) {
            g_wall_src_w = file_header[0];
            g_wall_src_h = file_header[1];

            if (g_wall_src_w > 0 && g_wall_src_h > 0 && 
                g_wall_src_w <= 1920 && g_wall_src_h <= 1080) {
                
                UINT32 total_pixels = g_wall_src_w * g_wall_src_h;
                UINT32 read_bytes = (total_pixels + 2) * sizeof(UINT32);

                int bytes = fat32_read_file(info->port_addr, "WALL.BIN", (void*)g_wall_buffer, read_bytes);
                if (bytes > 8) {
                    for (UINT32 k = 0; k < total_pixels; k++) {
                        g_wall_buffer[k] = g_wall_buffer[k + 2];
                    }
                    g_wall_loaded = 1;
                    return;
                }
            }
        }
    }
}

static void draw_desktop_background(void) {
    if (!g_wall_loaded) {
        load_wallpaper_from_disk();
    }

    if (g_wall_loaded && g_backbuffer != NULL && g_wall_buffer != NULL && gop_width > 0 && gop_height > 0) {
        if (g_wall_src_w == gop_width && g_wall_src_h == gop_height) {
            UINTN total = gop_width * gop_height;
            for (UINTN i = 0; i < total; i++) {
                g_backbuffer[i] = g_wall_buffer[i];
            }
            return;
        } else {
            for (UINTN y = 0; y < gop_height; y++) {
                UINTN src_y = (y * g_wall_src_h) / gop_height;
                UINT32 *dst_row = &g_backbuffer[y * gop_width];
                const UINT32 *src_row = &g_wall_buffer[src_y * g_wall_src_w];

                for (UINTN x = 0; x < gop_width; x++) {
                    UINTN src_x = (x * g_wall_src_w) / gop_width;
                    dst_row[x] = src_row[src_x];
                }
            }
            return;
        }
    }

    draw_filled_rect(0, 0, gop_width, gop_height, 0x000E1017);
}

static void execute_start_menu_item(int item) {
    desktop_menu_open = 0;
    wm_mark_all_dirty();

    int total_app_entries = (disk_app_count > 0 ? disk_app_count : 1);

    if (item < disk_app_count) {
        if (str_equal_nocase(disk_apps[item], "CALC.BIN")) {
            calc_app_launch();
        }
    } else if (item == total_app_entries) {
        system_reboot();
    } else if (item == total_app_entries + 1) {
        system_shutdown();
    }
}

static void handle_mouse_events(void) {
    MouseState *m = mouse_get_state();
    int mx = m->x;
    int my = m->y;
    int taskbar_y = (int)gop_height - 42;

    if (!m->left_button && wm_is_dragging()) {
        wm_stop_drag();
    }

    if (wm_is_dragging()) {
        wm_update_drag(mx, my);
        return;
    }

    if (!m->left_clicked) return;

    /* Start Button */
    if (mx >= 8 && mx <= 103 && my >= taskbar_y + 5 && my <= taskbar_y + 37) {
        desktop_menu_open = !desktop_menu_open;
        if (desktop_menu_open) scan_disk_applications();
        start_menu_selected = 0;
        wm_mark_all_dirty();
        return;
    }

    /* Power Button */
    int power_w = 85;
    int power_x = (int)gop_width - power_w - 10;
    if (mx >= power_x && mx <= power_x + power_w && my >= taskbar_y + 5 && my <= taskbar_y + 37) {
        system_shutdown();
        return;
    }

    /* Window Tabs */
    int tab_x = 112;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        Window *win = wm_get_window(i);
        if (!win || win->is_closed) continue;
        if (mx >= tab_x && mx <= tab_x + 160 && my >= taskbar_y + 5 && my <= taskbar_y + 37) {
            wm_focus_window(i);
            wm_mark_all_dirty();
            return;
        }
        tab_x += 168;
    }

    /* Startmenü */
    if (desktop_menu_open) {
        int total_entries = (disk_app_count > 0 ? disk_app_count : 1) + 2;
        int menu_w = 260;
        int menu_h = 38 + (total_entries * 28) + 8;
        int menu_x = 8;
        int menu_y = taskbar_y - menu_h - 6;

        if (mx >= menu_x && mx <= menu_x + menu_w && my >= menu_y && my <= menu_y + menu_h) {
            int item_y = menu_y + 38;
            for (int i = 0; i < total_entries; i++) {
                if (my >= item_y - 2 && my <= item_y + 24) {
                    execute_start_menu_item(i);
                    return;
                }
                item_y += 28;
                if (i == (disk_app_count > 0 ? disk_app_count : 1) - 1) {
                    item_y += 6;
                }
            }
        } else {
            desktop_menu_open = 0;
            wm_mark_all_dirty();
        }
    }

    /* Fenster Header & Drag & Drop */
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        Window *win = wm_get_window(i);
        if (!win || win->is_closed || win->is_minimized) continue;

        if (mx >= win->x && mx <= win->x + win->width &&
            my >= win->y && my <= win->y + win->height) {
            
            wm_focus_window(i);

            if (mx >= win->x + win->width - 28 && mx <= win->x + win->width - 6 &&
                my >= win->y + 6 && my <= win->y + 28) {
                wm_close_window(i);
            }
            else if (mx >= win->x + win->width - 54 && mx <= win->x + win->width - 32 &&
                     my >= win->y + 6 && my <= win->y + 28) {
                wm_minimize_window(i);
            }
            else if (my >= win->y && my <= win->y + 34) {
                wm_start_drag(i, mx, my);
            }
            else if (mx >= win->x + 12 && mx <= win->x + win->width - 12 &&
                     my >= win->y + 40 && my <= win->y + win->height - 12) {
                if (win->on_click) {
                    win->on_click(win->id, mx - (win->x + 12), my - (win->y + 40));
                }
            }

            wm_mark_all_dirty();
            return;
        }
    }
}

void desktop_tick_frame(void) {
    net_poll();
    NetworkState *net = net_get_state();

    if (net->http_synced != last_synced_state) {
        last_synced_state = net->http_synced;
        wm_mark_all_dirty();
    }

    int mouse_moved = mouse_update();
    MouseState *m = mouse_get_state();

    if (mouse_moved || m->left_clicked || wm_is_dragging()) {
        handle_mouse_events();
        wm_mark_dirty(last_mouse_x - 2, last_mouse_y - 2, 20, 26);
        wm_mark_dirty(m->x - 2, m->y - 2, 20, 26);
        last_mouse_x = m->x;
        last_mouse_y = m->y;
    }

    // Volles Datum & Uhrzeit auslesen
    int year = 2025, month = 5, day = 1, raw_h = 12, m_t = 0, s = 0;
    get_rtc_full_datetime(&year, &month, &day, &raw_h, &m_t, &s);

    // Automatische Sommerzeit-Berechnung (MESZ / CEST = UTC+2 vs. MEZ / CET = UTC+1)
    int offset = net->tz_offset_hours;
    const char *tz_label = net->timezone_abbr;

    // Für Deutschland/Zentraleuropa dynamisch zwischen CET (+1) und CEST (+2) umschalten
    if (str_equal_nocase(net->country_code, "DE") || str_equal_nocase(net->country_code, "AT") || 
        str_equal_nocase(net->country_code, "CH") || str_equal_nocase(net->country_code, "LOC")) {
        if (is_european_summer_time(year, month, day, raw_h)) {
            offset = 2; // Sommerzeit MESZ (UTC+2)
            tz_label = "CEST";
        } else {
            offset = 1; // Winterzeit MEZ (UTC+1)
            tz_label = "CET";
        }
    }

    // Exakte Ortszeit berechnen
    int h = (raw_h + offset + 24) % 24;

    if (s != last_second) {
        last_second = s;
        wm_mark_dirty((int)gop_width - 270, (int)gop_height - 44, 270, 44);
    }

    if (!wm_is_dirty()) {
        return;
    }

    /* 1. Wallpaper */
    draw_desktop_background();

    /* 2. Top-Statusleiste (Live Standort & Zeitzone) */
    draw_filled_rect(0, 0, gop_width, 30, 0x000F172A);
    draw_filled_rect(0, 29, gop_width, 1, 0x001E293B);

    wm_draw_text("VeloOS 64-Bit Bare-Metal", 14, 7, 0x0038BDF8, 0x00000000);

    // Standortanzeige mit Live-Zeitzone
    char loc_info[80];
    char *lp = loc_info;
    *lp++ = '['; *lp++ = 'O'; *lp++ = 'r'; *lp++ = 't'; *lp++ = ':'; *lp++ = ' ';
    for (int i = 0; net->city[i]; i++) *lp++ = net->city[i];
    *lp++ = ','; *lp++ = ' ';
    for (int i = 0; net->country_code[i]; i++) *lp++ = net->country_code[i];
    *lp++ = ' '; *lp++ = '|'; *lp++ = ' ';
    for (int i = 0; tz_label[i]; i++) *lp++ = tz_label[i];
    *lp++ = ' '; *lp++ = '('; *lp++ = 'U'; *lp++ = 'T'; *lp++ = 'C';
    *lp++ = (offset >= 0) ? '+' : '-';
    int abs_off = offset >= 0 ? offset : -offset;
    *lp++ = '0' + (char)abs_off;
    *lp++ = ')'; *lp++ = ']'; *lp = '\0';
    
    wm_draw_text(loc_info, 240, 7, net->http_synced ? 0x004ADE80 : 0x00FACC15, 0x00000000);

    wm_draw_text("[F1] Startmenue  |  [TAB] Fenster  |  [ESC] Abmelden", (int)gop_width - 440, 7, 0x00F1F5F9, 0x00000000);

    /* 3. Fenster */
    wm_render_all();

    /* 4. Taskleiste */
    int taskbar_h = 42;
    int taskbar_y = (int)gop_height - taskbar_h;

    draw_filled_rect(0, taskbar_y, gop_width, taskbar_h, 0x000F172A);
    draw_filled_rect(0, taskbar_y, gop_width, 1, 0x00334155);

    /* Start Button */
    UINT32 sb_top = desktop_menu_open ? 0x002563EB : 0x001D4ED8;
    UINT32 sb_bot = desktop_menu_open ? 0x001D4ED8 : 0x001E40AF;
    draw_rounded_rect_gradient(8, taskbar_y + 5, 95, 32, 6, sb_top, sb_bot);
    draw_rounded_rect_aa(8, taskbar_y + 5, 95, 32, 6, 0x0060A5FA);
    draw_rounded_rect_gradient(10, taskbar_y + 7, 91, 28, 4, sb_top, sb_bot);
    wm_draw_text("[ Start ]", 20, taskbar_y + 13, 0x00FFFFFF, 0x00000000);

    /* Tabs */
    int tab_x = 112;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        Window *win = wm_get_window(i);
        if (!win || win->is_closed) continue;

        UINT32 tb_bg = win->is_active ? 0x001E293B : 0x00111827;
        UINT32 tb_border = win->is_active ? 0x0038BDF8 : 0x00374151;
        UINT32 tb_text = win->is_active ? 0x00FFFFFF : 0x0094A3B8;

        draw_rounded_rect_aa(tab_x, taskbar_y + 5, 160, 32, 6, tb_border);
        draw_rounded_rect_aa(tab_x + 1, taskbar_y + 6, 158, 30, 5, tb_bg);
        wm_draw_text(win->title, tab_x + 12, taskbar_y + 13, tb_text, 0x00000000);
        tab_x += 168;
    }

    /* Power Button & Uhrzeit (mit Sommer-/Winterzeit-Tag) */
    int power_w = 85;
    int power_x = (int)gop_width - power_w - 10;
    int clock_w = 135;
    int clock_x = power_x - clock_w - 10;

    char time_str[24] = {
        '0' + (char)(h / 10), '0' + (char)(h % 10), ':',
        '0' + (char)(m_t / 10), '0' + (char)(m_t % 10), ':',
        '0' + (char)(s / 10), '0' + (char)(s % 10), ' ',
        tz_label[0], tz_label[1], tz_label[2], tz_label[3], '\0'
    };

    draw_rounded_rect_aa(clock_x, taskbar_y + 5, clock_w, 32, 6, 0x00334155);
    draw_rounded_rect_aa(clock_x + 1, taskbar_y + 6, clock_w - 2, 30, 5, 0x001E293B);
    wm_draw_text(time_str, clock_x + 14, taskbar_y + 13, 0x0038BDF8, 0x00000000);

    draw_rounded_rect_gradient(power_x, taskbar_y + 5, power_w, 32, 6, 0x00DC2626, 0x00991B1B);
    draw_rounded_rect_aa(power_x, taskbar_y + 5, power_w, 32, 6, 0x00F87171);
    draw_rounded_rect_gradient(power_x + 1, taskbar_y + 6, power_w - 2, 30, 5, 0x00DC2626, 0x00991B1B);
    wm_draw_text("Power", power_x + 22, taskbar_y + 13, 0x00FFFFFF, 0x00000000);

    /* 5. Startmenü */
    if (desktop_menu_open) {
        int total_entries = (disk_app_count > 0 ? disk_app_count : 1) + 2;
        int menu_w = 260;
        int menu_h = 38 + (total_entries * 28) + 8;
        int menu_x = 8;
        int menu_y = taskbar_y - menu_h - 6;

        draw_rounded_rect_aa(menu_x + 4, menu_y + 4, menu_w, menu_h, 8, 0x00020617);
        draw_rounded_rect_aa(menu_x, menu_y, menu_w, menu_h, 8, 0x00334155);
        draw_rounded_rect_aa(menu_x + 1, menu_y + 1, menu_w - 2, menu_h - 2, 7, 0x000F172A);

        draw_rounded_rect_gradient(menu_x + 2, menu_y + 2, menu_w - 4, 30, 6, 0x002563EB, 0x001D4ED8);
        wm_draw_text("Programme (FAT32)", menu_x + 14, menu_y + 9, 0x00FFFFFF, 0x00000000);

        int item_y = menu_y + 38;
        int cur_idx = 0;

        if (disk_app_count == 0) {
            wm_draw_text("[Keine Apps auf Disk]", menu_x + 14, item_y + 4, 0x0064748B, 0x000F172A);
            item_y += 28;
            cur_idx++;
        } else {
            for (int i = 0; i < disk_app_count; i++) {
                if (cur_idx == start_menu_selected) {
                    draw_rounded_rect_aa(menu_x + 6, item_y - 2, menu_w - 12, 24, 4, 0x002563EB);
                    wm_draw_text(disk_apps[i], menu_x + 14, item_y + 2, 0x00FFFFFF, 0x002563EB);
                } else {
                    wm_draw_text(disk_apps[i], menu_x + 14, item_y + 2, 0x00E2E8F0, 0x000F172A);
                }
                item_y += 28;
                cur_idx++;
            }
        }

        draw_filled_rect(menu_x + 10, item_y, menu_w - 20, 1, 0x00334155);
        item_y += 6;

        const char *sys_items[2] = {"Neustart", "Herunterfahren"};
        for (int s_idx = 0; s_idx < 2; s_idx++) {
            if (cur_idx == start_menu_selected) {
                draw_rounded_rect_aa(menu_x + 6, item_y - 2, menu_w - 12, 24, 4, 0x00DC2626);
                wm_draw_text(sys_items[s_idx], menu_x + 14, item_y + 2, 0x00FFFFFF, 0x00DC2626);
            } else {
                wm_draw_text(sys_items[s_idx], menu_x + 14, item_y + 2, 0x00F87171, 0x000F172A);
            }
            item_y += 28;
            cur_idx++;
        }
    }

    /* 6. Mauszeiger */
    mouse_draw_cursor();

    /* 7. Blit */
    int dx, dy, dw, dh;
    wm_get_dirty_bounds(&dx, &dy, &dw, &dh);
    swap_buffers_rect(dx, dy, dw, dh);
    wm_clear_dirty();
}

void desktop_start(void) {
    desktop_menu_open = 0;
    start_menu_selected = 0;
    system_mode = 3;

    net_init();
    mouse_init();
    scan_disk_applications();
    load_wallpaper_from_disk();
    wm_init();
    wm_mark_all_dirty();
    desktop_tick_frame();
}

void desktop_handle_key(char c) {
    int total_entries = (disk_app_count > 0 ? disk_app_count : 1) + 2;

    if (desktop_menu_open) {
        if (c == 0x1B) {
            desktop_menu_open = 0;
            wm_mark_all_dirty();
            return;
        }
        if (c == 'w' || c == 'W') {
            start_menu_selected = (start_menu_selected - 1 + total_entries) % total_entries;
            wm_mark_all_dirty();
            return;
        }
        if (c == 's' || c == 'S') {
            start_menu_selected = (start_menu_selected + 1) % total_entries;
            wm_mark_all_dirty();
            return;
        }
        if (c == '\n' || c == '\r') {
            execute_start_menu_item(start_menu_selected);
            return;
        }
    }

    int active_id = wm_get_active_window_id();
    if (active_id >= 0) {
        Window *win = wm_get_window(active_id);
        if (win && win->on_key) {
            win->on_key(win->id, c);
        }
    }

    if (c == 0x1B) {
        desktop_menu_open = 0;
        system_mode = 0;
        logged_in = 0;
        clear_screen_graphics(0x00000000);
        wm_draw_text("VeloOS Kernel - Bare Metal Active", 50, 30, 0x00FFFFFF, 0x00000000);
        wm_draw_text("Abgemeldet. Zurueck in der Shell.", 50, 60, 0x0038BDF8, 0x00000000);
        gfx_cursor_x = 50; gfx_cursor_y = 120;
        draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
        gfx_cursor_x += 32;
        swap_buffers();
        return;
    }

    if ((unsigned char)c == 0x3B || (unsigned char)c == 0xF1) {
        desktop_menu_open = !desktop_menu_open;
        if (desktop_menu_open) {
            scan_disk_applications();
        }
        start_menu_selected = 0;
        wm_mark_all_dirty();
        return;
    }

    if (c == 0x09) { wm_focus_next(); return; }
    if (c == 'm' || c == 'M') { wm_minimize_active(); return; }
    if (c == 'x' || c == 'X') { wm_close_active(); return; }
}
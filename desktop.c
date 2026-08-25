#include <efi.h>
#include <efilib.h>
#include "desktop.h"
#include "font.h"
#include "wm.h"
#include "mouse.h"
#include "keyboard.h"
#include "net.h"
#include "ahci.h"
#include "fat32.h"
#include "setup.h"
#include "syscall.h"

extern UINTN gop_width;
extern UINTN gop_height;
extern UINT32 *g_backbuffer;
extern char g_user_name_active[32];
extern UINT64 g_total_ram_mb;
extern char g_cpu_brand[49];

void *memmove(void *dest, const void *src, UINTN n);
void put_pixel(UINTN x, UINTN y, UINT32 color);
void draw_filled_rect(UINTN start_x, UINTN start_y, UINTN width, UINTN height, UINT32 color);
void draw_string(const char* str, UINTN x, UINTN y, UINT32 fg_color, UINT32 bg_color);
void clear_screen_graphics(UINT32 color);
void swap_buffers_rect(int rx, int ry, int rw, int rh);
void system_shutdown(void);
void system_reboot(void);

static int start_menu_open = 0;
static int start_menu_selected = 0;
static int last_second = -1;
static int last_mouse_x = -1;
static int last_mouse_y = -1;

typedef struct {
    char text[128];
    int len;
    int cursor_pos;
    int scroll_offset;
    int max_visible;
} WindowsTextBox;

static WindowsTextBox g_search_box = {"", 0, 0, 0, 24};
static int g_search_focused = 0;

static char g_notepad_text[512] = "Willkommen in Velo!\nHier koennen Sie Notizen schreiben. Dieser Text bricht automatisch am Zeilenende sauber um.";
static int g_notepad_len = 100;

#define TOTAL_APPS 4
static const char *g_all_apps[TOTAL_APPS] = {
    "Velo Explorer",
    "Editor (Notepad)",
    "Systeminformationen",
    "Systemsteuerung"
};

static int str_contains_nocase(const char *haystack, const char *needle) {
    if (!needle || !needle[0]) return 1;
    for (int i = 0; haystack[i] != '\0'; i++) {
        int match = 1;
        for (int k = 0; needle[k] != '\0'; k++) {
            if (haystack[i+k] == '\0') { match = 0; break; }
            char h = (haystack[i+k] >= 'A' && haystack[i+k] <= 'Z') ? (haystack[i+k] + 32) : haystack[i+k];
            char n = (needle[k] >= 'A' && needle[k] <= 'Z') ? (needle[k] + 32) : needle[k];
            if (h != n) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

static void draw_velo_start(int cx, int cy, int r, int active) {
    draw_rounded_rect_aa(cx - r, cy - r, r * 2, r * 2, r, active ? 0x005AC0E0 : 0x002B5268);
    draw_rounded_rect_gradient(cx - r + 2, cy - r + 2, (r - 2) * 2, (r - 2) * 2, r - 2, 0x001B62D6, 0x000A2C68);
    draw_rounded_rect_aa(cx - r + 6, cy - r + 4, (r - 6) * 2, r - 4, (r - 6), 0x0060A5FA);

    int size = r / 2;
    draw_line_aa(cx - size, cy - size + 2, cx - size / 4, cy + size, 3, 0x00FFFFFF);
    draw_line_aa(cx - size / 4, cy + size, cx + size - 2, cy - size, 3, 0x00FFFFFF);
    draw_line_aa(cx - size - 2, cy + 1, cx + size + 4, cy - 2, 3, 0x005AC0E0);
    draw_line_aa(cx - size - 1, cy + 1, cx + size + 2, cy - 2, 1, 0x00FFFFFF);
}

static inline unsigned char inb_cmos(unsigned short port) {
    unsigned char res;
    __asm__ volatile("inb %1, %0" : "=a"(res) : "Nd"(port));
    return res;
}
static inline void outb_cmos(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void get_rtc_time(int *day, int *month, int *year, int *h, int *m, int *s) {
    outb_cmos(0x70, 0x00); *s = inb_cmos(0x71);
    outb_cmos(0x70, 0x02); *m = inb_cmos(0x71);
    outb_cmos(0x70, 0x04); *h = inb_cmos(0x71);
    outb_cmos(0x70, 0x07); *day = inb_cmos(0x71);
    outb_cmos(0x70, 0x08); *month = inb_cmos(0x71);
    outb_cmos(0x70, 0x09); *year = inb_cmos(0x71);

    *s = (*s & 0x0F) + ((*s >> 4) * 10);
    *m = (*m & 0x0F) + ((*m >> 4) * 10);
    *h = (*h & 0x0F) + ((*h >> 4) * 10);
    *day = (*day & 0x0F) + ((*day >> 4) * 10);
    *month = (*month & 0x0F) + ((*month >> 4) * 10);
    *year = (*year & 0x0F) + ((*year >> 4) * 10) + 2000;
}

static int is_dst_european(int year, int month, int day, int hour) {
    if (month < 3 || month > 10) return 0;
    if (month > 3 && month < 10) return 1;

    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int y = (month < 3) ? year - 1 : year;
    int day_of_31 = (y + y/4 - y/100 + y/400 + t[month - 1] + 31) % 7;
    int last_sunday = 31 - day_of_31;

    if (month == 3) return (day > last_sunday || (day == last_sunday && hour >= 1));
    else return (day < last_sunday || (day == last_sunday && hour < 1));
}

static void notepad_on_paint(int win_id, int cx, int cy, int cw, int ch) {
    (void)win_id;
    draw_rounded_rect_aa(cx + 4, cy + 4, cw - 8, ch - 8, 4, 0x00FFFFFF);
    wm_draw_text_wrapped(g_notepad_text, cx + 10, cy + 10, cw - 20, ch - 20, 0x000F172A, 0x00FFFFFF);
}

static void notepad_on_key(int win_id, char key) {
    (void)win_id;
    char k = keyboard_translate_char(key);
    if (k == '\b') {
        if (g_notepad_len > 0) g_notepad_text[--g_notepad_len] = '\0';
    } else if ((unsigned char)k >= 32 && g_notepad_len < 500) {
        g_notepad_text[g_notepad_len++] = k;
        g_notepad_text[g_notepad_len] = '\0';
    } else if (k == '\n' && g_notepad_len < 500) {
        g_notepad_text[g_notepad_len++] = '\n';
        g_notepad_text[g_notepad_len] = '\0';
    }
    wm_mark_all_dirty();
}

static void sysinfo_on_paint(int win_id, int cx, int cy, int cw, int ch) {
    (void)win_id; (void)ch;
    wm_draw_text("Velo Ultimate (64-Bit Edition)", cx + 12, cy + 12, 0x000F172A, 0x00000000);
    draw_filled_rect(cx + 12, cy + 30, cw - 24, 1, 0x00D0D8E0);

    char cpu_str[80];
    int pos = 0;
    const char *c_lbl = "Prozessor: "; while(*c_lbl) cpu_str[pos++] = *c_lbl++;
    const char *cb = g_cpu_brand; while(*cb && pos < 74) cpu_str[pos++] = *cb++;
    cpu_str[pos] = '\0';
    wm_draw_text_wrapped(cpu_str, cx + 12, cy + 42, cw - 24, 36, 0x00334155, 0x00000000);

    char ram_str[48];
    pos = 0;
    const char *r_lbl = "Arbeitsspeicher: "; while(*r_lbl) ram_str[pos++] = *r_lbl++;
    if (g_total_ram_mb >= 1000) {
        ram_str[pos++] = '0' + (char)(g_total_ram_mb / 1000);
        ram_str[pos++] = ' '; ram_str[pos++] = 'G'; ram_str[pos++] = 'B';
    } else {
        if (g_total_ram_mb >= 100) ram_str[pos++] = '0' + (char)(g_total_ram_mb / 100);
        if (g_total_ram_mb >= 10) ram_str[pos++] = '0' + (char)((g_total_ram_mb / 10) % 10);
        ram_str[pos++] = '0' + (char)(g_total_ram_mb % 10);
        ram_str[pos++] = ' '; ram_str[pos++] = 'M'; ram_str[pos++] = 'B';
    }
    ram_str[pos] = '\0';
    wm_draw_text(ram_str, cx + 12, cy + 80, 0x00334155, 0x00000000);
}

static void control_panel_on_paint(int win_id, int cx, int cy, int cw, int ch) {
    (void)win_id; (void)ch;
    wm_draw_text("Systemsteuerung - Uebersicht", cx + 12, cy + 12, 0x000F172A, 0x00000000);
    wm_draw_text_wrapped("-> Benutzerkonten & Kennwoerter: Aktiv konfiguriert\n-> Darstellung & Design: Velo Aero Glasseffekte\n-> Zeit & Region: Deutschland (Automatische Sommerzeit)", cx + 12, cy + 38, cw - 24, 80, 0x002563EB, 0x00000000);
}

static void execute_app_action(int idx) {
    start_menu_open = 0;
    g_search_focused = 0;
    wm_mark_all_dirty();

    if (idx == 0) {
        load_and_run_app("EXPLORER.BIN");
    } else if (idx == 1) {
        int wid = wm_create_window_auto("Editor (Notepad)", 440, 220, notepad_on_paint);
        if (wid >= 0) {
            Window *w = wm_get_window(wid);
            if (w) w->on_key = notepad_on_key;
        }
    } else if (idx == 2) {
        wm_create_window_auto("Systeminformationen", 440, 160, sysinfo_on_paint);
    } else if (idx == 3) {
        wm_create_window_auto("Systemsteuerung", 440, 150, control_panel_on_paint);
    }
}

static void handle_mouse_events(void) {
    MouseState *m = mouse_get_state();
    int mx = m->x;
    int my = m->y;
    int taskbar_y = (int)gop_height - 38;

    int cursor = CURSOR_ARROW;
    if ((mx >= 4 && mx <= 44 && my >= taskbar_y - 4 && my <= taskbar_y + 36) ||
        (start_menu_open && mx <= 380 && my >= taskbar_y - 380)) {
        if (start_menu_open && mx >= 8 && mx <= 224 && my >= taskbar_y - 36 && my <= taskbar_y - 8) {
            cursor = CURSOR_IBEAM;
        } else {
            cursor = CURSOR_HAND;
        }
    }
    mouse_set_cursor(cursor);

    if (!m->left_button && wm_is_dragging()) {
        wm_stop_drag();
    }
    if (wm_is_dragging()) {
        wm_update_drag(mx, my);
        return;
    }
    if (!m->left_clicked) return;

    /* Start Orb */
    if (mx >= 4 && mx <= 44 && my >= taskbar_y - 4 && my <= taskbar_y + 36) {
        start_menu_open = !start_menu_open;
        start_menu_selected = 0;
        g_search_focused = 0;
        wm_mark_all_dirty();
        return;
    }

    /* Startmenü Klicks */
    if (start_menu_open) {
        int menu_w = 380;
        int menu_h = 380;
        int menu_x = 0;
        int menu_y = taskbar_y - menu_h;

        if (mx >= menu_x && mx <= menu_x + menu_w && my >= menu_y && my <= menu_y + menu_h) {
            if (mx >= menu_x + 8 && mx <= menu_x + 224 && my >= menu_y + menu_h - 36 && my <= menu_y + menu_h - 8) {
                g_search_focused = 1;
                wm_mark_all_dirty();
                return;
            }

            int item_y = menu_y + 16;
            for (int i = 0; i < TOTAL_APPS; i++) {
                if (str_contains_nocase(g_all_apps[i], g_search_box.text)) {
                    if (mx <= menu_x + 220 && my >= item_y && my <= item_y + 34) {
                        execute_app_action(i);
                        return;
                    }
                    item_y += 38;
                }
            }

            if (mx >= menu_x + 228 && mx <= menu_x + menu_w) {
                if (my >= menu_y + 16 && my <= menu_y + 40) execute_app_action(0);
                else if (my >= menu_y + 46 && my <= menu_y + 70) execute_app_action(2);
                else if (my >= menu_y + 76 && my <= menu_y + 100) execute_app_action(3);
            }

            if (my >= menu_y + menu_h - 36) {
                if (mx >= menu_x + menu_w - 75) system_shutdown();
                else if (mx >= menu_x + menu_w - 155) system_reboot();
            }
            return;
        } else {
            start_menu_open = 0;
            g_search_focused = 0;
            wm_mark_all_dirty();
        }
    }

    /* Fenster Klick-Verarbeitung */
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        Window *win = wm_get_window(i);
        if (!win || win->is_closed || win->is_minimized) continue;

        if (mx >= win->x && mx <= win->x + win->width && my >= win->y && my <= win->y + win->height) {
            wm_focus_window(i);

            // 1. [X] Schließen Button
            if (mx >= win->x + win->width - 30 && mx <= win->x + win->width - 4 && my >= win->y + 5 && my <= win->y + 25) {
                wm_close_window(i);
            } 
            // 2. [□] Maximieren / Wiederherstellen Button
            else if (mx >= win->x + win->width - 58 && mx <= win->x + win->width - 32 && my >= win->y + 5 && my <= win->y + 25) {
                wm_maximize_window(i);
            } 
            // 3. [-] Minimieren Button
            else if (mx >= win->x + win->width - 86 && mx <= win->x + win->width - 60 && my >= win->y + 5 && my <= win->y + 25) {
                wm_minimize_window(i);
            } 
            // 4. Titelleiste ziehen (Drag & Drop)
            else if (my >= win->y && my <= win->y + 30) {
                wm_start_drag(i, mx, my);
            } 
            // 5. Inhaltsbereich
            else if (win->on_click) {
                win->on_click(win->id, mx - (win->x + 8), my - (win->y + 34));
            }
            wm_mark_all_dirty();
            return;
        }
    }
}

void desktop_tick_frame(void) {
    net_poll();
    int mouse_moved = mouse_update();
    MouseState *m = mouse_get_state();

    if (mouse_moved || m->left_clicked || wm_is_dragging()) {
        handle_mouse_events();
        wm_mark_dirty(last_mouse_x - 4, last_mouse_y - 4, 28, 32);
        wm_mark_dirty(m->x - 4, m->y - 4, 28, 32);
        last_mouse_x = m->x;
        last_mouse_y = m->y;
    }

    int day = 1, month = 1, year = 2025, raw_h = 12, m_t = 0, s = 0;
    get_rtc_time(&day, &month, &year, &raw_h, &m_t, &s);

    int offset = 1;
    const char *tz_str = "MEZ";
    if (is_dst_european(year, month, day, raw_h)) {
        offset = 2;
        tz_str = "MESZ";
    }
    int h = (raw_h + offset + 24) % 24;

    if (s != last_second) {
        last_second = s;
        wm_mark_dirty((int)gop_width - 180, (int)gop_height - 38, 180, 38);
    }

    if (!wm_is_dirty()) return;

    /* 1. Hintergrund */
    draw_rounded_rect_gradient(0, 0, (int)gop_width, (int)gop_height, 0, 0x000B3648, 0x0002141C);

    /* 2. Fenster */
    wm_render_all();

    /* 3. Taskleiste */
    int taskbar_h = 38;
    int taskbar_y = (int)gop_height - taskbar_h;

    draw_rounded_rect_gradient(0, taskbar_y, (int)gop_width, taskbar_h, 0, 0x00142A36, 0x00081018);
    draw_filled_rect(0, taskbar_y, gop_width, 1, 0x004A8FA8);

    int tab_x = 52;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        Window *win = wm_get_window(i);
        if (!win || win->is_closed) continue;

        UINT32 t_top = win->is_active ? 0x002B5268 : 0x00142834;
        UINT32 t_bot = win->is_active ? 0x00142A36 : 0x000B141C;
        draw_rounded_rect_gradient(tab_x, taskbar_y + 4, 150, 30, 4, t_top, t_bot);
        draw_rounded_rect_aa(tab_x, taskbar_y + 4, 150, 30, 4, win->is_active ? 0x005AC0E0 : 0x002B5268);
        wm_draw_text(win->title, tab_x + 10, taskbar_y + 11, win->is_active ? 0x00FFFFFF : 0x0094A3B8, 0x00000000);
        tab_x += 156;
    }

    int tray_x = (int)gop_width - 160;
    draw_rounded_rect_gradient(tray_x, taskbar_y + 4, 152, 30, 4, 0x0010202A, 0x00081018);
    char time_str[24] = {
        '0' + (char)(h / 10), '0' + (char)(h % 10), ':',
        '0' + (char)(m_t / 10), '0' + (char)(m_t % 10), ':',
        '0' + (char)(s / 10), '0' + (char)(s % 10), ' ',
        tz_str[0], tz_str[1], tz_str[2], tz_str[3], '\0'
    };
    wm_draw_text(time_str, tray_x + 10, taskbar_y + 11, 0x00FFFFFF, 0x00000000);

    /* 4. Velo Start Orb */
    draw_velo_start(24, taskbar_y + 19, 18, start_menu_open);

    /* 5. Velo Startmenü */
    if (start_menu_open) {
        int menu_w = 380;
        int menu_h = 380;
        int menu_x = 0;
        int menu_y = taskbar_y - menu_h;

        draw_rounded_rect_aa(menu_x, menu_y, menu_w, menu_h, 8, 0x004A8FA8);
        draw_rounded_rect_aa(menu_x + 1, menu_y + 1, menu_w - 2, menu_h - 2, 7, 0x000B1A24);

        draw_filled_rect(menu_x + 6, menu_y + 6, 220, menu_h - 48, 0x00F8FAFC);
        int item_y = menu_y + 16;
        int visible_idx = 0;

        for (int i = 0; i < TOTAL_APPS; i++) {
            if (str_contains_nocase(g_all_apps[i], g_search_box.text)) {
                if (visible_idx == start_menu_selected) {
                    draw_rounded_rect_aa(menu_x + 10, item_y - 2, 212, 28, 4, 0x003B82F6);
                    wm_draw_text(g_all_apps[i], menu_x + 18, item_y + 4, 0x00FFFFFF, 0x003B82F6);
                } else {
                    wm_draw_text(g_all_apps[i], menu_x + 18, item_y + 4, 0x000F172A, 0x00F8FAFC);
                }
                item_y += 38;
                visible_idx++;
            }
        }

        draw_filled_rect(menu_x + 228, menu_y + 6, 146, menu_h - 48, 0x00142A36);
        wm_draw_text(g_user_name_active, menu_x + 236, menu_y + 16, 0x00FFFFFF, 0x00000000);
        wm_draw_text("Dokumente", menu_x + 236, menu_y + 46, 0x0094A3B8, 0x00000000);
        wm_draw_text("Computer", menu_x + 236, menu_y + 76, 0x00FFFFFF, 0x00000000);
        wm_draw_text("System", menu_x + 236, menu_y + 106, 0x0094A3B8, 0x00000000);

        UINT32 s_border = g_search_focused ? 0x003B82F6 : 0x00CBD5E1;
        draw_rounded_rect_aa(menu_x + 8, menu_y + menu_h - 36, 216, 28, 4, s_border);
        draw_rounded_rect_aa(menu_x + 9, menu_y + menu_h - 35, 214, 26, 3, 0x00FFFFFF);

        if (g_search_box.len > 0) {
            char visible_buf[32];
            int v_len = g_search_box.len - g_search_box.scroll_offset;
            if (v_len > g_search_box.max_visible) v_len = g_search_box.max_visible;

            for (int k = 0; k < v_len; k++) {
                visible_buf[k] = g_search_box.text[g_search_box.scroll_offset + k];
            }
            visible_buf[v_len] = '\0';
            wm_draw_text(visible_buf, menu_x + 16, menu_y + menu_h - 28, 0x000F172A, 0x00FFFFFF);

            if (g_search_focused) {
                int cur_x_vis = menu_x + 16 + (g_search_box.cursor_pos - g_search_box.scroll_offset) * 8;
                draw_filled_rect(cur_x_vis, menu_y + menu_h - 28, 1, 14, 0x000F172A);
            }
        } else {
            wm_draw_text("Suche starten...", menu_x + 16, menu_y + menu_h - 28, 0x0094A3B8, 0x00FFFFFF);
        }

        draw_rounded_rect_gradient(menu_x + menu_w - 150, menu_y + menu_h - 36, 68, 28, 4, 0x002B5268, 0x00142A36);
        wm_draw_text("Neustart", menu_x + menu_w - 146, menu_y + menu_h - 28, 0x00FFFFFF, 0x00000000);

        draw_rounded_rect_gradient(menu_x + menu_w - 74, menu_y + menu_h - 36, 68, 28, 4, 0x00DC2626, 0x00991B1B);
        wm_draw_text("Power", menu_x + menu_w - 62, menu_y + menu_h - 28, 0x00FFFFFF, 0x00000000);
    }

    mouse_draw_cursor();

    int dx, dy, dw, dh;
    wm_get_dirty_bounds(&dx, &dy, &dw, &dh);
    swap_buffers_rect(dx, dy, dw, dh);
    wm_clear_dirty();
}

void desktop_start(void) {
    start_menu_open = 0;
    start_menu_selected = 0;
    g_search_focused = 0;
    g_search_box.text[0] = '\0';
    g_search_box.len = 0;
    g_search_box.cursor_pos = 0;
    g_search_box.scroll_offset = 0;

    net_init();
    mouse_init();
    wm_init();
    wm_mark_all_dirty();
    desktop_tick_frame();
}

void desktop_handle_key(char c) {
    if (c == KEY_SUPER || c == KEY_F1 || c == KEY_ESC) {
        start_menu_open = !start_menu_open;
        start_menu_selected = 0;
        g_search_focused = 0;
        wm_mark_all_dirty();
        return;
    }

    if (start_menu_open) {
        if (g_search_focused) {
            if (c == KEY_LEFT) {
                if (g_search_box.cursor_pos > 0) {
                    g_search_box.cursor_pos--;
                    if (g_search_box.cursor_pos < g_search_box.scroll_offset) {
                        g_search_box.scroll_offset = g_search_box.cursor_pos;
                    }
                }
                wm_mark_all_dirty();
                return;
            }
            else if (c == KEY_RIGHT) {
                if (g_search_box.cursor_pos < g_search_box.len) {
                    g_search_box.cursor_pos++;
                    if (g_search_box.cursor_pos - g_search_box.scroll_offset > g_search_box.max_visible) {
                        g_search_box.scroll_offset = g_search_box.cursor_pos - g_search_box.max_visible;
                    }
                }
                wm_mark_all_dirty();
                return;
            }
            else if (c == KEY_HOME) {
                g_search_box.cursor_pos = 0;
                g_search_box.scroll_offset = 0;
                wm_mark_all_dirty();
                return;
            }
            else if (c == KEY_END) {
                g_search_box.cursor_pos = g_search_box.len;
                if (g_search_box.len > g_search_box.max_visible) {
                    g_search_box.scroll_offset = g_search_box.len - g_search_box.max_visible;
                }
                wm_mark_all_dirty();
                return;
            }
            else if (c == '\b') {
                if (g_search_box.cursor_pos > 0) {
                    int p = g_search_box.cursor_pos - 1;
                    int count = g_search_box.len - p;
                    memmove(&g_search_box.text[p], &g_search_box.text[p + 1], (UINTN)count);
                    g_search_box.len--;
                    g_search_box.cursor_pos--;
                    if (g_search_box.cursor_pos < g_search_box.scroll_offset) {
                        g_search_box.scroll_offset = g_search_box.cursor_pos;
                    }
                    start_menu_selected = 0;
                }
                wm_mark_all_dirty();
                return;
            }
            else if ((unsigned char)c >= 32 && g_search_box.len < 120) {
                char tc = keyboard_translate_char(c);
                int p = g_search_box.cursor_pos;
                int count = g_search_box.len - p + 1;
                memmove(&g_search_box.text[p + 1], &g_search_box.text[p], (UINTN)count);
                g_search_box.text[p] = tc;
                g_search_box.len++;
                g_search_box.cursor_pos++;
                if (g_search_box.cursor_pos - g_search_box.scroll_offset > g_search_box.max_visible) {
                    g_search_box.scroll_offset = g_search_box.cursor_pos - g_search_box.max_visible;
                }
                start_menu_selected = 0;
                wm_mark_all_dirty();
                return;
            }
        }

        if (c == KEY_UP || c == 'w' || c == 'W') {
            start_menu_selected = (start_menu_selected - 1 + TOTAL_APPS) % TOTAL_APPS;
            wm_mark_all_dirty();
            return;
        }
        if (c == KEY_DOWN || c == 's' || c == 'S') {
            start_menu_selected = (start_menu_selected + 1) % TOTAL_APPS;
            wm_mark_all_dirty();
            return;
        }
        if (c == '\n' || c == '\r') {
            int cur = 0;
            for (int i = 0; i < TOTAL_APPS; i++) {
                if (str_contains_nocase(g_all_apps[i], g_search_box.text)) {
                    if (cur == start_menu_selected) {
                        execute_app_action(i);
                        return;
                    }
                    cur++;
                }
            }
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
}
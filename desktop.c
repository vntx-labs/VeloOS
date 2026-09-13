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
#include "sched.h"
#include <velo/icons.h>

extern EFI_SYSTEM_TABLE *g_st;
extern UINTN gop_width;
extern UINTN gop_height;
extern UINT32 *g_backbuffer;
extern char g_user_name_active[32];
extern UINT64 g_total_ram_mb;
extern char g_cpu_brand[49];

void put_pixel(UINTN x, UINTN y, UINT32 color);
void draw_filled_rect(UINTN start_x, UINTN start_y, UINTN width, UINTN height, UINT32 color);
void clear_screen_graphics(UINT32 color);
void swap_buffers(void);

static void desktop_icon_set_pixel(int px, int py, unsigned int color) {
    if (px >= 0 && px < (int)gop_width && py >= 0 && py < (int)gop_height) {
        put_pixel((UINTN)px, (UINTN)py, (UINT32)color);
    }
}

static inline int kstrcmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return -1;
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static inline int str_ends_with_nocase(const char *name, const char *ext) {
    if (!name || !ext) return 0;
    int nlen = 0; while (name[nlen]) nlen++;
    int elen = 0; while (ext[elen]) elen++;
    if (nlen < elen) return 0;
    for (int i = 0; i < elen; i++) {
        char c1 = name[nlen - elen + i], c2 = ext[i];
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return 0;
    }
    return 1;
}

static inline int is_executable(const char *name) {
    return str_ends_with_nocase(name, ".bin") || str_ends_with_nocase(name, ".BIN") ||
           str_ends_with_nocase(name, ".efi") || str_ends_with_nocase(name, ".EFI");
}

static inline void outw_io(unsigned short port, unsigned short val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outb_io(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void system_shutdown(void) {
    if (RT && RT->ResetSystem) {
        uefi_call_wrapper(RT->ResetSystem, 4, EfiResetShutdown, EFI_SUCCESS, 0, NULL);
    }
    outw_io(0x604, 0x2000);
    outw_io(0xB004, 0x2000);
    outw_io(0x4004, 0x3400);
    while (1) { __asm__ volatile("cli; hlt"); }
}

static void system_reboot(void) {
    if (RT && RT->ResetSystem) {
        uefi_call_wrapper(RT->ResetSystem, 4, EfiResetCold, EFI_SUCCESS, 0, NULL);
    }
    outb_io(0x64, 0xFE);
    outb_io(0xCF9, 0x06);
    while (1) { __asm__ volatile("cli; hlt"); }
}

static int start_menu_open = 0;
static int start_menu_selected = 0;
static int last_second = -1;

#define MAX_DESKTOP_ICONS 128
static VeloDirEntry g_desktop_files[MAX_DESKTOP_ICONS];
static int g_desktop_file_count = 0;
static int g_selected_desktop_icon = -1;
static int g_last_click_icon = -1;

static int g_ctx_menu_open = 0;
static int g_ctx_menu_x = 0, g_ctx_menu_y = 0;

#define CTX_MENU_W 190
#define CTX_MENU_H 140
#define CTX_ITEM_COUNT 4

static const char *g_ctx_labels[CTX_ITEM_COUNT] = {
    "+ Neuer Ordner",
    "+ Neues Dokument",
    "~ Aktualisieren",
    "[PC] Explorer"
};

typedef struct {
    char text[128];
    int len, cursor_pos, scroll_offset, max_visible;
} WindowsTextBox;

static WindowsTextBox g_search_box = {"", 0, 0, 0, 24};
static int g_search_focused = 0;

/* =========================================================================
 * DYNAMISCHES LADEN AUS DEM ORDNER /Programs
 * ========================================================================= */
#define MAX_PROGRAM_APPS 64
typedef struct {
    char name[48];
    char path[128];
} ProgramAppInfo;

static ProgramAppInfo g_program_apps[MAX_PROGRAM_APPS];
static int g_program_app_count = 0;

static void* get_primary_drive_port(void) {
    int port_count = ahci_get_port_count();
    for (int p = 0; p < port_count; p++) {
        AHCI_PORT_INFO *info = ahci_get_port_info(p);
        if (info && info->active) return info->port_addr;
    }
    return NULL;
}

static void desktop_reload_program_apps(void) {
    void *port = get_primary_drive_port();
    if (!port) return;

    g_program_app_count = 0;
    VeloDirEntry raw_entries[MAX_PROGRAM_APPS];
    int cnt = fat32_list_dir(port, "/Programs", raw_entries, MAX_PROGRAM_APPS);
    if (cnt <= 0) {
        cnt = fat32_list_dir(port, "C:/Programs", raw_entries, MAX_PROGRAM_APPS);
    }

    for (int i = 0; i < cnt && g_program_app_count < MAX_PROGRAM_APPS; i++) {
        if (!raw_entries[i].is_dir && is_executable(raw_entries[i].name)) {
            char *dname = g_program_apps[g_program_app_count].name;
            const char *sname = raw_entries[i].name;

            // Dateiendung fuer schoene Anzeige im Startmenue entfernen
            int p = 0;
            while (sname[p] && sname[p] != '.' && p < 47) {
                dname[p] = sname[p];
                p++;
            }
            dname[p] = '\0';

            char *dpath = g_program_apps[g_program_app_count].path;
            const char *pfx = "C:/Programs/";
            int dp = 0;
            while (pfx[dp]) { dpath[dp] = pfx[dp]; dp++; }
            int sp = 0;
            while (sname[sp] && dp < 127) { dpath[dp++] = sname[sp++]; }
            dpath[dp] = '\0';

            g_program_app_count++;
        }
    }

    // Notfall-Fallback, falls das /Programs-Verzeichnis noch unbestueckt ist
    if (g_program_app_count == 0) {
        __builtin_memcpy(g_program_apps[0].name, "Explorer", 9);
        __builtin_memcpy(g_program_apps[0].path, "EXPLORER.BIN", 13);
        __builtin_memcpy(g_program_apps[1].name, "Editor (Notepad)", 17);
        __builtin_memcpy(g_program_apps[1].path, "NOTEPAD.BIN", 12);
        __builtin_memcpy(g_program_apps[2].name, "Web Browser", 12);
        __builtin_memcpy(g_program_apps[2].path, "BROWSER.BIN", 12);
        __builtin_memcpy(g_program_apps[3].name, "Eingabeaufforderung", 20);
        __builtin_memcpy(g_program_apps[3].path, "SH.BIN", 7);
        g_program_app_count = 4;
    }
}

static void desktop_reload_icons(void) {
    void *port = get_primary_drive_port();
    if (!port) return;

    g_desktop_file_count = 0;
    int count = fat32_list_dir(port, "/Users/Desktop", g_desktop_files, MAX_DESKTOP_ICONS);
    if (count <= 0) {
        count = fat32_list_dir(port, "C:/Users/Desktop", g_desktop_files, MAX_DESKTOP_ICONS);
    }
    if (count >= 0) g_desktop_file_count = count;
    if (g_selected_desktop_icon > g_desktop_file_count) g_selected_desktop_icon = -1;

    desktop_reload_program_apps();
    wm_mark_all_dirty();
}

static void desktop_delete_selected(void) {
    if (g_selected_desktop_icon <= 0) return;
    int file_idx = g_selected_desktop_icon - 1;
    if (file_idx < 0 || file_idx >= g_desktop_file_count) return;

    void *port = get_primary_drive_port();
    if (!port) return;

    char path[128]; int pos = 0;
    const char *prefix = "/Users/Desktop/";
    while (prefix[pos]) { path[pos] = prefix[pos]; pos++; }
    const char *name = g_desktop_files[file_idx].name;
    int k = 0; while (name[k] && pos < 120) path[pos++] = name[k++];
    path[pos] = '\0';

    fat32_delete_file(port, path);
    g_selected_desktop_icon = -1;
    desktop_reload_icons();
}

static void desktop_create_new_folder(void) {
    void *port = get_primary_drive_port();
    if (!port) return;

    char name[32] = "NeuerOrdner";
    char path[128]; int pos = 0;
    const char *prefix = "/Users/Desktop/";
    while (prefix[pos]) { path[pos] = prefix[pos]; pos++; }
    int k = 0; while (name[k]) path[pos++] = name[k++];
    path[pos] = '\0';

    fat32_mkdir(port, path);
    desktop_reload_icons();
}

static void desktop_create_new_file(void) {
    void *port = get_primary_drive_port();
    if (!port) return;

    char name[32] = "Neu.txt";
    char path[128]; int pos = 0;
    const char *prefix = "/Users/Desktop/";
    while (prefix[pos]) { path[pos] = prefix[pos]; pos++; }
    int k = 0; while (name[k]) path[pos++] = name[k++];
    path[pos] = '\0';

    char empty = '\0';
    fat32_write_file(port, path, &empty, 0);
    desktop_reload_icons();
}

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

static inline unsigned char inb_cmos_d(unsigned short port) {
    unsigned char res;
    __asm__ volatile("inb %1, %0" : "=a"(res) : "Nd"(port));
    return res;
}

static inline void outb_cmos_d(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void get_rtc_time(int *day, int *month, int *year, int *h, int *m, int *s) {
    outb_cmos_d(0x70, 0x00); *s = inb_cmos_d(0x71);
    outb_cmos_d(0x70, 0x02); *m = inb_cmos_d(0x71);
    outb_cmos_d(0x70, 0x04); *h = inb_cmos_d(0x71);
    outb_cmos_d(0x70, 0x07); *day = inb_cmos_d(0x71);
    outb_cmos_d(0x70, 0x08); *month = inb_cmos_d(0x71);
    outb_cmos_d(0x70, 0x09); *year = inb_cmos_d(0x71);

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

static void launch_explorer_at_path(const char *target_path) {
    void *port = get_primary_drive_port();
    if (!port) return;
    if (target_path && target_path[0]) {
        int len = 0; while (target_path[len]) len++;
        fat32_write_file(port, "/VeloOS/System32/STARTDIR.DAT", (void*)target_path, (UINT32)len);
        fat32_write_file(port, "/STARTDIR.DAT", (void*)target_path, (UINT32)len);
    } else {
        fat32_delete_file(port, "/VeloOS/System32/STARTDIR.DAT");
        fat32_delete_file(port, "/STARTDIR.DAT");
    }
    task_spawn_app("EXPLORER.BIN");
}

static void open_file_in_notepad(const char *path) {
    void *port = get_primary_drive_port();
    if (!port) return;
    if (path && path[0]) {
        int len = 0; while (path[len]) len++;
        fat32_write_file(port, "/VeloOS/System32/NOTEPAD_FILE.DAT", (void*)path, (UINT32)len);
        fat32_write_file(port, "/NOTEPAD_FILE.DAT", (void*)path, (UINT32)len);
    } else {
        fat32_delete_file(port, "/VeloOS/System32/NOTEPAD_FILE.DAT");
        fat32_delete_file(port, "/NOTEPAD_FILE.DAT");
    }
    task_spawn_app("NOTEPAD.BIN");
}

static void execute_app_action(int idx) {
    start_menu_open = 0;
    g_search_focused = 0;
    g_ctx_menu_open = 0;
    wm_mark_all_dirty();

    if (idx >= 0 && idx < g_program_app_count) {
        task_spawn_app(g_program_apps[idx].path);
    }
}

static void open_desktop_item(int icon_idx) {
    if (icon_idx == 0) {
        launch_explorer_at_path("Computer");
    } else {
        int file_idx = icon_idx - 1;
        if (file_idx >= 0 && file_idx < g_desktop_file_count) {
            VeloDirEntry *cur = &g_desktop_files[file_idx];
            char full_p[128]; int pos = 0;
            const char *prefix = "C:/Users/Desktop/";
            while (prefix[pos]) { full_p[pos] = prefix[pos]; pos++; }
            int k = 0; while (cur->name[k] && pos < 120) full_p[pos++] = cur->name[k++];
            full_p[pos] = '\0';

            if (cur->is_dir) launch_explorer_at_path(full_p);
            else if (is_executable(cur->name)) task_spawn_app(full_p);
            else open_file_in_notepad(full_p);
        }
    }
}

static inline int in_box(int px, int py, int bx, int by, int bw, int bh) {
    return (px >= bx && px < bx + bw && py >= by && py < by + bh);
}

static void handle_mouse_events(void) {
    MouseState *m = mouse_get_state();
    int mx = m->x, my = m->y;
    int taskbar_y = (int)gop_height - 38;
    int cursor = CURSOR_ARROW;

    if (my >= taskbar_y) cursor = CURSOR_HAND;
    else if (start_menu_open && in_box(mx, my, 0, taskbar_y - 380, 380, 380)) {
        if (in_box(mx, my, 8, taskbar_y - 36, 216, 28)) cursor = CURSOR_IBEAM;
        else cursor = CURSOR_HAND;
    }
    mouse_set_cursor(cursor);

    if (!m->left_button && wm_is_dragging()) wm_stop_drag();
    if (wm_is_dragging()) { wm_update_drag(mx, my); return; }

    // Fenster-Klicks
    for (int i = wm_get_window_count() - 1; i >= 0; i--) {
        Window *win = wm_get_window(i);
        if (!win || win->is_closed || win->is_minimized) continue;

        if (in_box(mx, my, win->x, win->y, win->width, win->height)) {
            if (start_menu_open) {
                start_menu_open = 0;
                g_search_focused = 0;
                wm_mark_all_dirty();
            }

            int btn_top = win->is_maximized ? 4 : 5;
            if (m->left_clicked && in_box(mx, my, win->x + win->width - 30, win->y + btn_top, 26, 20)) {
                m->left_clicked = 0; wm_close_window(i); return;
            } else if (m->left_clicked && in_box(mx, my, win->x + win->width - 58, win->y + btn_top, 26, 20)) {
                m->left_clicked = 0; wm_maximize_window(i); return;
            } else if (m->left_clicked && in_box(mx, my, win->x + win->width - 86, win->y + btn_top, 26, 20)) {
                m->left_clicked = 0; wm_minimize_window(i); return;
            }

            if (m->left_clicked || m->right_clicked) wm_focus_window(i);

            if (m->left_clicked && in_box(mx, my, win->x, win->y, win->width, 30)) {
                m->left_clicked = 0; wm_start_drag(i, mx, my); return;
            }

            int off_x = win->is_maximized ? 0 : 6;
            int off_y = 30;
            int local_x = mx - (win->x + off_x);
            int local_y = my - (win->y + off_y);

            if (m->left_clicked) {
                m->left_clicked = 0;
                wm_window_push_event(i, 1 /* VELO_EV_CLICK */, local_x, local_y, 0, 0, 0);
                task_push_event(i, 1, local_x, local_y, 0);
            } else if (m->right_clicked) {
                m->right_clicked = 0;
                wm_window_push_event(i, 4 /* VELO_EV_RCLICK */, local_x, local_y, 0, 0, 0);
                task_push_event(i, 4, local_x, local_y, 0);
            } else if (m->scroll_z != 0) {
                wm_window_push_event(i, 5 /* VELO_EV_SCROLL */, local_x, local_y, 0, 0, m->scroll_z);
                task_push_event(i, 5, local_x, local_y, 0);
                m->scroll_z = 0;
            }
            return;
        }
    }

    // Rechtsklick Desktop
    if (m->right_clicked && my < taskbar_y) {
        m->right_clicked = 0;
        start_menu_open = 0;
        g_search_focused = 0;
        g_ctx_menu_open = 1;
        g_ctx_menu_x = (mx + CTX_MENU_W > (int)gop_width) ? (int)gop_width - CTX_MENU_W - 4 : mx;
        g_ctx_menu_y = (my + CTX_MENU_H > taskbar_y) ? taskbar_y - CTX_MENU_H - 4 : my;
        wm_mark_all_dirty();
        return;
    }

    if (!m->left_clicked) return;

    // Kontextmenue Klicks
    if (g_ctx_menu_open) {
        m->left_clicked = 0;
        if (in_box(mx, my, g_ctx_menu_x, g_ctx_menu_y, CTX_MENU_W, CTX_MENU_H)) {
            int item_idx = (my - (g_ctx_menu_y + 4)) / 28;
            g_ctx_menu_open = 0;
            if (item_idx == 0) desktop_create_new_folder();
            else if (item_idx == 1) desktop_create_new_file();
            else if (item_idx == 2) desktop_reload_icons();
            else if (item_idx == 3) launch_explorer_at_path("Computer");
            wm_mark_all_dirty();
            return;
        }
        g_ctx_menu_open = 0;
        wm_mark_all_dirty();
    }

    // Start-Orb Klick
    if (in_box(mx, my, 4, taskbar_y - 4, 40, 40)) {
        m->left_clicked = 0;
        start_menu_open = !start_menu_open;
        start_menu_selected = 0;
        g_search_focused = 0;
        g_ctx_menu_open = 0;
        if (start_menu_open) desktop_reload_program_apps();
        wm_mark_all_dirty();
        return;
    }

    // Taskleisten Tabs
    if (my >= taskbar_y && my <= (int)gop_height && !start_menu_open) {
        int tab_x = 52;
        for (int i = 0; i < MAX_WINDOWS; i++) {
            Window *win = wm_get_window(i);
            if (!win || win->is_closed) continue;
            if (mx >= tab_x && mx <= tab_x + 150) {
                m->left_clicked = 0;
                if (win->is_minimized) wm_minimize_window(i);
                else if (win->is_active) wm_minimize_window(i);
                else wm_focus_window(i);
                return;
            }
            tab_x += 156;
        }
    }

    // Startmenue Klicks
    if (start_menu_open) {
        int menu_w = 380, menu_h = 380, menu_x = 0, menu_y = taskbar_y - menu_h;
        if (mx >= menu_x && mx <= menu_x + menu_w && my >= menu_y && my <= menu_y + menu_h) {
            if (my >= menu_y + menu_h - 36 && mx >= menu_x + menu_w - 74) {
                m->left_clicked = 0; system_shutdown(); return;
            }
            if (my >= menu_y + menu_h - 36 && mx >= menu_x + menu_w - 150 && mx < menu_x + menu_w - 74) {
                m->left_clicked = 0; system_reboot(); return;
            }

            m->left_clicked = 0;
            if (mx >= menu_x + 8 && mx <= menu_x + 224 && my >= menu_y + menu_h - 36 && my <= menu_y + menu_h - 8) {
                g_search_focused = 1; wm_mark_all_dirty(); return;
            } else {
                g_search_focused = 0;
            }

            int item_y = menu_y + 16;
            for (int i = 0; i < g_program_app_count; i++) {
                if (str_contains_nocase(g_program_apps[i].name, g_search_box.text)) {
                    if (mx <= menu_x + 220 && my >= item_y && my <= item_y + 34) {
                        execute_app_action(i); return;
                    }
                    item_y += 38;
                }
            }

            if (mx >= menu_x + 228 && mx <= menu_x + menu_w) {
                if (my >= menu_y + 16 && my <= menu_y + 40) {
                    launch_explorer_at_path("C:/Users");
                    start_menu_open = 0; wm_mark_all_dirty(); return;
                } else if (my >= menu_y + 46 && my <= menu_y + 70) {
                    launch_explorer_at_path("C:/Users/Documents");
                    start_menu_open = 0; wm_mark_all_dirty(); return;
                } else if (my >= menu_y + 76 && my <= menu_y + 100) {
                    launch_explorer_at_path("Computer");
                    start_menu_open = 0; wm_mark_all_dirty(); return;
                }
            }
            return;
        } else {
            m->left_clicked = 0; start_menu_open = 0; g_search_focused = 0; wm_mark_all_dirty();
        }
    }

    // Desktop Icons Klick
    int total_icons = 1 + g_desktop_file_count;
    int max_rows = ((int)gop_height - 60) / 80;
    if (max_rows <= 0) max_rows = 1;

    for (int i = 0; i < total_icons; i++) {
        int col = i / max_rows, row = i % max_rows;
        int ix = 20 + col * 88, iy = 20 + row * 82;
        if (in_box(mx, my, ix, iy, 76, 74)) {
            m->left_clicked = 0;
            if (g_selected_desktop_icon == i && g_last_click_icon == i) {
                open_desktop_item(i); g_last_click_icon = -1;
            } else {
                g_selected_desktop_icon = i; g_last_click_icon = i;
            }
            wm_mark_all_dirty(); return;
        }
    }
    g_selected_desktop_icon = -1; g_last_click_icon = -1; wm_mark_all_dirty();
}

void desktop_start(void) {
    start_menu_open = 0; start_menu_selected = 0;
    g_search_focused = 0; g_ctx_menu_open = 0;
    g_selected_desktop_icon = -1; g_last_click_icon = -1;
    g_search_box.text[0] = '\0'; g_search_box.len = 0;
    g_search_box.cursor_pos = 0; g_search_box.scroll_offset = 0;

    mouse_init();
    wm_init();
    desktop_reload_icons();
    wm_mark_all_dirty();
    desktop_tick_frame();
}

void desktop_tick_frame(void) {
    int mouse_moved = mouse_update();
    MouseState *m = mouse_get_state();

    if (mouse_moved || m->left_clicked || m->right_clicked || wm_is_dragging()) {
        handle_mouse_events();
        wm_mark_all_dirty();
    }

    int day = 1, month = 1, year = 2025, raw_h = 12, m_t = 0, s = 0;
    get_rtc_time(&day, &month, &year, &raw_h, &m_t, &s);
    int offset = is_dst_european(year, month, day, raw_h) ? 2 : 1;
    const char *tz_str = (offset == 2) ? "MESZ" : "MEZ";
    int h = (raw_h + offset + 24) % 24;

    if (s != last_second) {
        last_second = s;
        wm_mark_all_dirty();
    }

    if (!wm_is_dirty()) return;

    draw_rounded_rect_gradient(0, 0, (int)gop_width, (int)gop_height, 0, 0x000B3648, 0x0002141C);

    int total_icons = 1 + g_desktop_file_count;
    int max_rows = ((int)gop_height - 60) / 80;
    if (max_rows <= 0) max_rows = 1;

    for (int i = 0; i < total_icons; i++) {
        int col = i / max_rows, row = i % max_rows;
        int ix = 20 + col * 88, iy = 20 + row * 82;

        if (g_selected_desktop_icon == i) {
            draw_rounded_rect_aa(ix, iy, 76, 74, 6, 0x0038BDF8);
            draw_rounded_rect_gradient(ix + 1, iy + 1, 74, 72, 5, 0x001B485A, 0x000B2430);
        }

        const char *name_str = "Computer";
        if (i == 0) {
            name_str = "Computer";
            render_icon_pc_32(ix + 22, iy + 6, desktop_icon_set_pixel);
        } else {
            int file_idx = i - 1;
            if (file_idx >= 0 && file_idx < g_desktop_file_count) {
                VeloDirEntry *cur = &g_desktop_files[file_idx];
                name_str = cur->name;
                if (cur->is_dir) render_icon_folder_32(ix + 22, iy + 6, desktop_icon_set_pixel);
                else if (is_executable(cur->name)) render_icon_app_32(ix + 22, iy + 6, desktop_icon_set_pixel);
                else if (str_ends_with_nocase(cur->name, ".txt") || str_ends_with_nocase(cur->name, ".dat"))
                    render_icon_doc_32(ix + 22, iy + 6, desktop_icon_set_pixel);
                else render_icon_file_32(ix + 22, iy + 6, desktop_icon_set_pixel);
            }
        }

        char short_name[12]; int np = 0;
        while (name_str[np] && np < 9) { short_name[np] = name_str[np]; np++; }
        if (name_str[np]) { short_name[7] = '.'; short_name[8] = '.'; short_name[9] = '\0'; }
        else short_name[np] = '\0';

        int tx = ix + (76 - np * 8) / 2;
        if (tx < ix + 4) tx = ix + 4;
        wm_draw_text_shadow(short_name, tx, iy + 44, 1, 0x00FFFFFF, 0x0002060C);
    }

    wm_render_all();

    int taskbar_h = 38, taskbar_y = (int)gop_height - taskbar_h;
    draw_rounded_rect_gradient(0, taskbar_y, (int)gop_width, taskbar_h, 0, 0x00142A36, 0x00081018);
    draw_filled_rect(0, (UINTN)taskbar_y, gop_width, 1, 0x004A8FA8);

    draw_velo_start(24, taskbar_y + 19, 18, start_menu_open);

    int tab_x = 52;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        Window *win = wm_get_window(i);
        if (!win || win->is_closed) continue;

        int is_current = win->is_active && !win->is_minimized;
        UINT32 t_top = is_current ? 0x002B5268 : (win->is_minimized ? 0x000C1820 : 0x00183040);
        UINT32 t_bot = is_current ? 0x00142A36 : (win->is_minimized ? 0x00081014 : 0x000E1C26);
        
        draw_rounded_rect_gradient(tab_x, taskbar_y + 4, 150, 30, 4, t_top, t_bot);
        draw_rounded_rect_aa(tab_x, taskbar_y + 4, 150, 30, 4, is_current ? 0x005AC0E0 : (win->is_minimized ? 0x001E3A4A : 0x003A6880));
        
        char tab_title[18]; int tp = 0;
        while (win->title[tp] && tp < 15) { tab_title[tp] = win->title[tp]; tp++; }
        if (win->title[tp]) { tab_title[14] = '.'; tab_title[15] = '.'; }
        tab_title[tp] = '\0';

        wm_draw_text(tab_title, tab_x + 10, taskbar_y + 11, is_current ? 0x00FFFFFF : (win->is_minimized ? 0x0064748B : 0x00CBD5E1), 0x00000000);
        tab_x += 156;
    }

    int tray_x = (int)gop_width - 160;
    draw_rounded_rect_gradient(tray_x, taskbar_y + 4, 152, 30, 4, 0x0010202A, 0x00081018);
    
    char time_str[24] = {
        (char)('0' + (h / 10)), (char)('0' + (h % 10)), ':',
        (char)('0' + (m_t / 10)), (char)('0' + (m_t % 10)), ':',
        (char)('0' + (s / 10)), (char)('0' + (s % 10)), ' ',
        tz_str[0], tz_str[1], tz_str[2], tz_str[3], '\0'
    };
    wm_draw_text(time_str, tray_x + 10, taskbar_y + 11, 0x00FFFFFF, 0x00000000);

    // DYNAMISCHES VISTA-STARTMENUE
    if (start_menu_open) {
        int menu_w = 380, menu_h = 380, menu_x = 0, menu_y = taskbar_y - menu_h;
        draw_rounded_rect_aa(menu_x, menu_y, menu_w, menu_h, 8, 0x004A8FA8);
        draw_rounded_rect_aa(menu_x + 1, menu_y + 1, menu_w - 2, menu_h - 2, 7, 0x000B1A24);

        draw_filled_rect((UINTN)(menu_x + 6), (UINTN)(menu_y + 6), 220, (UINTN)(menu_h - 48), 0x00F8FAFC);
        int item_y = menu_y + 16, visible_idx = 0;

        for (int i = 0; i < g_program_app_count; i++) {
            if (str_contains_nocase(g_program_apps[i].name, g_search_box.text)) {
                if (visible_idx == start_menu_selected) {
                    draw_rounded_rect_aa(menu_x + 10, item_y - 2, 212, 28, 4, 0x003B82F6);
                    wm_draw_text(g_program_apps[i].name, menu_x + 18, item_y + 4, 0x00FFFFFF, 0x003B82F6);
                } else {
                    wm_draw_text(g_program_apps[i].name, menu_x + 18, item_y + 4, 0x000F172A, 0x00F8FAFC);
                }
                item_y += 38;
                visible_idx++;
                if (item_y > menu_y + menu_h - 60) break;
            }
        }

        draw_filled_rect((UINTN)(menu_x + 228), (UINTN)(menu_y + 6), 146, (UINTN)(menu_h - 48), 0x00142A36);
        wm_draw_text(g_user_name_active, menu_x + 236, menu_y + 16, 0x00FFFFFF, 0x00000000);
        wm_draw_text("Dokumente", menu_x + 236, menu_y + 46, 0x0094A3B8, 0x00000000);
        wm_draw_text("Computer", menu_x + 236, menu_y + 76, 0x00FFFFFF, 0x00000000);

        UINT32 s_border = g_search_focused ? 0x003B82F6 : 0x00CBD5E1;
        draw_rounded_rect_aa(menu_x + 8, menu_y + menu_h - 36, 216, 28, 4, s_border);
        draw_rounded_rect_aa(menu_x + 9, menu_y + menu_h - 35, 214, 26, 3, 0x00FFFFFF);

        if (g_search_box.len > 0) {
            char visible_buf[32];
            int v_len = g_search_box.len - g_search_box.scroll_offset;
            if (v_len > g_search_box.max_visible) v_len = g_search_box.max_visible;
            for (int k = 0; k < v_len; k++) visible_buf[k] = g_search_box.text[g_search_box.scroll_offset + k];
            visible_buf[v_len] = '\0';
            wm_draw_text(visible_buf, menu_x + 16, menu_y + menu_h - 28, 0x000F172A, 0x00FFFFFF);
        } else {
            wm_draw_text("Suche starten...", menu_x + 16, menu_y + menu_h - 28, 0x0094A3B8, 0x00FFFFFF);
        }

        draw_rounded_rect_gradient(menu_x + menu_w - 150, menu_y + menu_h - 36, 68, 28, 4, 0x002B5268, 0x00142A36);
        wm_draw_text("Neustart", menu_x + menu_w - 146, menu_y + menu_h - 28, 0x00FFFFFF, 0x00000000);

        draw_rounded_rect_gradient(menu_x + menu_w - 74, menu_y + menu_h - 36, 68, 28, 4, 0x00DC2626, 0x00991B1B);
        wm_draw_text("Power", menu_x + menu_w - 62, menu_y + menu_h - 28, 0x00FFFFFF, 0x00000000);
    }

    if (g_ctx_menu_open) {
        int cx = g_ctx_menu_x, cy = g_ctx_menu_y;
        draw_rounded_rect_aa(cx + 3, cy + 3, CTX_MENU_W, CTX_MENU_H, 6, 0x0002060C);
        draw_rounded_rect_aa(cx, cy, CTX_MENU_W, CTX_MENU_H, 6, 0x004A8FA8);
        draw_rounded_rect_gradient(cx + 1, cy + 1, CTX_MENU_W - 2, CTX_MENU_H - 2, 5, 0x00142A36, 0x000B1A24);

        int item_y = cy + 4;
        for (int i = 0; i < CTX_ITEM_COUNT; i++) {
            if (in_box(m->x, m->y, cx + 2, item_y, CTX_MENU_W - 4, 26)) {
                draw_rounded_rect_aa(cx + 4, item_y, CTX_MENU_W - 8, 26, 4, 0x0038BDF8);
                draw_rounded_rect_gradient(cx + 5, item_y + 1, CTX_MENU_W - 10, 24, 3, 0x001B62D6, 0x000A2C68);
                wm_draw_text(g_ctx_labels[i], cx + 12, item_y + 6, 0x00FFFFFF, 0x00000000);
            } else {
                wm_draw_text(g_ctx_labels[i], cx + 12, item_y + 6, 0x00CBD5E1, 0x00000000);
            }
            item_y += 28;
        }
    }

    mouse_draw_cursor();
    swap_buffers();
    wm_clear_dirty();
}

void desktop_handle_key(char c) {
    if (c == KEY_SUPER || c == KEY_F1) {
        start_menu_open = !start_menu_open;
        start_menu_selected = 0;
        g_search_focused = 0;
        g_ctx_menu_open = 0;
        if (start_menu_open) desktop_reload_program_apps();
        wm_mark_all_dirty();
        return;
    }
    if (g_ctx_menu_open && c == KEY_ESC) {
        g_ctx_menu_open = 0; wm_mark_all_dirty(); return;
    }

    if (start_menu_open) {
        if (c == KEY_ESC) {
            start_menu_open = 0; g_search_focused = 0; wm_mark_all_dirty(); return;
        }
        if (g_search_focused) {
            if (c == '\b') {
                if (g_search_box.len > 0) {
                    g_search_box.text[--g_search_box.len] = '\0';
                    wm_mark_all_dirty();
                }
                return;
            } else if ((unsigned char)c >= 32 && g_search_box.len < 30) {
                g_search_box.text[g_search_box.len++] = c;
                g_search_box.text[g_search_box.len] = '\0';
                wm_mark_all_dirty();
                return;
            }
        }
        if (c == KEY_UP || c == 'w' || c == 'W') {
            if (g_program_app_count > 0) {
                start_menu_selected = (start_menu_selected - 1 + g_program_app_count) % g_program_app_count;
            }
            wm_mark_all_dirty(); return;
        }
        if (c == KEY_DOWN || c == 's' || c == 'S') {
            if (g_program_app_count > 0) {
                start_menu_selected = (start_menu_selected + 1) % g_program_app_count;
            }
            wm_mark_all_dirty(); return;
        }
        if (c == '\n' || c == '\r') {
            int cur = 0;
            for (int i = 0; i < g_program_app_count; i++) {
                if (str_contains_nocase(g_program_apps[i].name, g_search_box.text)) {
                    if (cur == start_menu_selected) { execute_app_action(i); return; }
                    cur++;
                }
            }
            return;
        }
    }

    int active_id = wm_get_active_window_id();
    if (active_id >= 0) {
        Window *win = wm_get_window(active_id);
        if (win && !win->is_closed && !win->is_minimized) {
            if (win->on_key) win->on_key(win->id, c);
            else {
                wm_window_push_event(active_id, 2 /* VELO_EV_KEY */, 0, 0, c, 0, 0);
                task_push_event(active_id, 2 /* VELO_EV_KEY */, 0, 0, c);
            }
        }
    } else {
        if (c == KEY_DELETE || (unsigned char)c == 0x88 || (unsigned char)c == 0x7F) {
            if (g_selected_desktop_icon > 0) desktop_delete_selected();
        } else if (g_selected_desktop_icon >= 0 && (c == '\n' || c == '\r')) {
            open_desktop_item(g_selected_desktop_icon);
        }
    }
}
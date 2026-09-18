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

/* Aus wallpaper.o gelinkte Binärdaten */
extern const char g_wallpaper_start[] __attribute__((weak));
extern const char g_wallpaper_end[] __attribute__((weak));

void put_pixel(UINTN x, UINTN y, UINT32 color);
UINT32 get_pixel(UINTN x, UINTN y);
void draw_filled_rect(UINTN start_x, UINTN start_y, UINTN width, UINTN height, UINT32 color);
void clear_screen_graphics(UINT32 color);
void swap_buffers(void);

static void desktop_icon_set_pixel(int px, int py, unsigned int color) {
    if (px >= 0 && px < (int)gop_width && py >= 0 && py < (int)gop_height) {
        put_pixel((UINTN)px, (UINTN)py, (UINT32)color);
    }
}

static inline int kstrlen(const char *s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

static inline int kstrcmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return -1;
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static inline int kstrcasecmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return (int)(unsigned char)c1 - (int)(unsigned char)c2;
        s1++; s2++;
    }
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

static inline int str_ends_with_nocase(const char *name, const char *ext) {
    if (!name || !ext) return 0;
    int nlen = kstrlen(name);
    int elen = kstrlen(ext);
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

static void strip_app_extension(const char *filename, char *out, int max_len) {
    if (!filename || !out || max_len <= 0) return;
    int len = kstrlen(filename);
    int dot_pos = -1;
    for (int i = len - 1; i >= 0; i--) {
        if (filename[i] == '.') {
            dot_pos = i;
            break;
        }
    }
    int copy_len = (dot_pos >= 0) ? dot_pos : len;
    if (copy_len >= max_len) copy_len = max_len - 1;
    for (int i = 0; i < copy_len; i++) {
        out[i] = filename[i];
    }
    out[copy_len] = '\0';
}

static void format_desktop_filename(const char *filename, char *out, int max_chars) {
    if (!filename || !out || max_chars <= 0) return;
    int len = kstrlen(filename);
    if (len <= max_chars) {
        for (int i = 0; i < len; i++) out[i] = filename[i];
        out[len] = '\0';
        return;
    }

    const char *dot = 0;
    for (int i = len - 1; i >= 0; i--) {
        if (filename[i] == '.') {
            dot = &filename[i];
            break;
        }
    }

    if (dot && dot != filename) {
        int ext_len = kstrlen(dot);
        int keep_front = max_chars - ext_len - 3;
        if (keep_front < 2) keep_front = 2;
        int p = 0;
        for (int i = 0; i < keep_front && p < max_chars; i++) out[p++] = filename[i];
        out[p++] = '.'; out[p++] = '.'; out[p++] = '.';
        for (int i = 0; dot[i] && p < max_chars; i++) out[p++] = dot[i];
        out[p] = '\0';
    } else {
        int keep = max_chars - 3;
        if (keep < 1) keep = 1;
        int p = 0;
        for (int i = 0; i < keep; i++) out[p++] = filename[i];
        out[p++] = '.'; out[p++] = '.'; out[p++] = '.';
        out[p] = '\0';
    }
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
#define DESKTOP_CELL_W 84
#define DESKTOP_CELL_H 88

static VeloDirEntry g_desktop_files[MAX_DESKTOP_ICONS];
static int g_desktop_file_count = 0;

static int g_icon_grid_col[MAX_DESKTOP_ICONS + 1];
static int g_icon_grid_row[MAX_DESKTOP_ICONS + 1];

static int g_drag_active = 0;
static int g_drag_start_mx = 0, g_drag_start_my = 0;
static int g_drag_cur_mx = 0, g_drag_cur_my = 0;
static int g_drag_origin_cols[MAX_DESKTOP_ICONS + 1];
static int g_drag_origin_rows[MAX_DESKTOP_ICONS + 1];

static int g_snapback_active = 0;
static int g_snapback_progress = 100;
static int g_snapback_from_x[MAX_DESKTOP_ICONS + 1];
static int g_snapback_from_y[MAX_DESKTOP_ICONS + 1];

static int g_dock_bounce_slot = -1;
static int g_dock_bounce_tick = 0;

static UINT8 g_selected_icons[MAX_DESKTOP_ICONS + 1] = {0};
static int g_last_click_icon = -1;
static int g_marquee_active = 0;
static int g_marquee_sx = 0, g_marquee_sy = 0;
static int g_marquee_ex = 0, g_marquee_ey = 0;

static int g_ctx_menu_open = 0;
static int g_ctx_menu_x = 0, g_ctx_menu_y = 0;

#define CTX_MENU_W 210
#define CTX_MENU_H 146
#define CTX_ITEM_COUNT 4

static const char *g_ctx_labels[CTX_ITEM_COUNT] = {
    "+ Neuer Ordner",
    "+ Neues Dokument",
    "~ Schreibtisch aktualisieren",
    "[PC] Computer oeffnen"
};

typedef struct {
    char text[128];
    int len, cursor_pos, scroll_offset, max_visible;
} WindowsTextBox;

static WindowsTextBox g_search_box = {"", 0, 0, 0, 24};
static int g_search_focused = 0;

#define MAX_PROGRAM_APPS 64
typedef struct {
    char name[48];
    char path[128];
    int icon_type;
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

static void desktop_save_ini(void) {
    void *port = get_primary_drive_port();
    if (!port) return;

    char ini_buf[4096];
    int pos = 0;

    const char *h1 = "[Layout]\r\n";
    while (*h1) ini_buf[pos++] = *h1++;

    int total_icons = 1 + g_desktop_file_count;
    for (int i = 0; i < total_icons; i++) {
        const char *prefix = "ID_";
        while (*prefix) ini_buf[pos++] = *prefix++;
        
        if (i >= 10) ini_buf[pos++] = '0' + (i / 10);
        ini_buf[pos++] = '0' + (i % 10);
        ini_buf[pos++] = '=';

        int c = g_icon_grid_col[i] + 1;
        int r = g_icon_grid_row[i] + 1;

        if (c >= 10) ini_buf[pos++] = '0' + (c / 10);
        ini_buf[pos++] = '0' + (c % 10);
        ini_buf[pos++] = ':';
        if (r >= 10) ini_buf[pos++] = '0' + (r / 10);
        ini_buf[pos++] = '0' + (r % 10);
        ini_buf[pos++] = '\r'; ini_buf[pos++] = '\n';
    }

    const char *h2 = "\r\n[Objects]\r\n";
    while (*h2) ini_buf[pos++] = *h2++;

    for (int i = 0; i < total_icons; i++) {
        const char *prefix = "ID_";
        while (*prefix) ini_buf[pos++] = *prefix++;
        if (i >= 10) ini_buf[pos++] = '0' + (i / 10);
        ini_buf[pos++] = '0' + (i % 10);
        ini_buf[pos++] = '=';

        if (i == 0) {
            const char *act = "NAME:Computer;TYPE:PC;ACTION:EXPLORER Computer\r\n";
            while (*act) ini_buf[pos++] = *act++;
        } else {
            int file_idx = i - 1;
            VeloDirEntry *cur = &g_desktop_files[file_idx];
            const char *npfx = "NAME:";
            while (*npfx) ini_buf[pos++] = *npfx++;
            const char *nm = cur->name;
            while (*nm && pos < 4000) ini_buf[pos++] = *nm++;
            
            if (cur->is_dir) {
                const char *tp = ";TYPE:FOLDER;ACTION:EXPLORER C:/Users/Desktop/";
                while (*tp) ini_buf[pos++] = *tp++;
                nm = cur->name;
                while (*nm && pos < 4000) ini_buf[pos++] = *nm++;
            } else if (is_executable(cur->name)) {
                const char *tp = ";TYPE:APP;ACTION:EXEC C:/Users/Desktop/";
                while (*tp) ini_buf[pos++] = *tp++;
                nm = cur->name;
                while (*nm && pos < 4000) ini_buf[pos++] = *nm++;
            } else {
                const char *tp = ";TYPE:DOC;ACTION:NOTEPAD C:/Users/Desktop/";
                while (*tp) ini_buf[pos++] = *tp++;
                nm = cur->name;
                while (*nm && pos < 4000) ini_buf[pos++] = *nm++;
            }
            ini_buf[pos++] = '\r'; ini_buf[pos++] = '\n';
        }
    }

    fat32_write_file(port, "/Users/Desktop/DESKTOP.INI", ini_buf, (UINT32)pos);
    fat32_write_file(port, "C:/Users/Desktop/DESKTOP.INI", ini_buf, (UINT32)pos);
}

static int desktop_load_ini(void) {
    void *port = get_primary_drive_port();
    if (!port) return 0;

    char ini_buf[4096];
    int n = fat32_read_file(port, "/Users/Desktop/DESKTOP.INI", ini_buf, sizeof(ini_buf) - 1);
    if (n <= 0) {
        n = fat32_read_file(port, "C:/Users/Desktop/DESKTOP.INI", ini_buf, sizeof(ini_buf) - 1);
    }
    if (n <= 0) return 0;

    ini_buf[n] = '\0';
    int total_icons = 1 + g_desktop_file_count;

    for (int i = 0; i < total_icons; i++) {
        char key[16];
        int kp = 0;
        key[kp++] = 'I'; key[kp++] = 'D'; key[kp++] = '_';
        if (i >= 10) key[kp++] = '0' + (i / 10);
        key[kp++] = '0' + (i % 10);
        key[kp++] = '=';
        key[kp] = '\0';

        char *found = NULL;
        for (int p = 0; p < n - kp; p++) {
            int match = 1;
            for (int k = 0; k < kp; k++) {
                if (ini_buf[p + k] != key[k]) { match = 0; break; }
            }
            if (match) { found = &ini_buf[p + kp]; break; }
        }

        if (found) {
            int c = 0, r = 0;
            while (*found >= '0' && *found <= '9') { c = c * 10 + (*found - '0'); found++; }
            if (*found == ':') found++;
            while (*found >= '0' && *found <= '9') { r = r * 10 + (*found - '0'); found++; }

            if (c > 0 && r > 0) {
                g_icon_grid_col[i] = c - 1;
                g_icon_grid_row[i] = r - 1;
            }
        }
    }
    return 1;
}

static void desktop_reload_program_apps(void) {
    void *port = get_primary_drive_port();
    if (!port) return;

    g_program_app_count = 0;
    const char *base_dirs[] = { "/Programs", "C:/Programs", "/apps", "C:/apps" };
    int num_base = (int)(sizeof(base_dirs) / sizeof(base_dirs[0]));

    for (int b = 0; b < num_base && g_program_app_count < MAX_PROGRAM_APPS; b++) {
        VeloDirEntry raw_entries[64];
        int cnt = fat32_list_dir(port, base_dirs[b], raw_entries, 64);
        if (cnt <= 0) continue;

        for (int i = 0; i < cnt && g_program_app_count < MAX_PROGRAM_APPS; i++) {
            if (!raw_entries[i].is_dir && is_executable(raw_entries[i].name)) {
                char app_label[48];
                strip_app_extension(raw_entries[i].name, app_label, sizeof(app_label));

                int exists = 0;
                for (int ex = 0; ex < g_program_app_count; ex++) {
                    if (!kstrcmp(g_program_apps[ex].name, app_label)) { exists = 1; break; }
                }

                if (!exists && app_label[0]) {
                    int ap = 0;
                    while (app_label[ap] && ap < 47) { g_program_apps[g_program_app_count].name[ap] = app_label[ap]; ap++; }
                    g_program_apps[g_program_app_count].name[ap] = '\0';

                    int dp = 0;
                    const char *bd = base_dirs[b];
                    while (*bd && dp < 120) g_program_apps[g_program_app_count].path[dp++] = *bd++;
                    g_program_apps[g_program_app_count].path[dp++] = '/';
                    const char *rn = raw_entries[i].name;
                    while (*rn && dp < 127) g_program_apps[g_program_app_count].path[dp++] = *rn++;
                    g_program_apps[g_program_app_count].path[dp] = '\0';

                    g_program_app_count++;
                }
            }
        }
    }
}

static void desktop_reload_icons(void) {
    void *port = get_primary_drive_port();
    if (!port) return;

    VeloDirEntry raw_files[MAX_DESKTOP_ICONS];
    int count = fat32_list_dir(port, "/Users/Desktop", raw_files, MAX_DESKTOP_ICONS);
    if (count <= 0) {
        count = fat32_list_dir(port, "C:/Users/Desktop", raw_files, MAX_DESKTOP_ICONS);
    }

    g_desktop_file_count = 0;
    if (count > 0) {
        for (int i = 0; i < count && g_desktop_file_count < MAX_DESKTOP_ICONS; i++) {
            if (kstrcasecmp(raw_files[i].name, "DESKTOP.INI") != 0) {
                g_desktop_files[g_desktop_file_count++] = raw_files[i];
            }
        }
    }

    int total_icons = 1 + g_desktop_file_count;
    int max_rows = ((int)gop_height - 90) / DESKTOP_CELL_H;
    if (max_rows <= 0) max_rows = 1;

    for (int i = 0; i < total_icons; i++) {
        g_icon_grid_col[i] = i / max_rows;
        g_icon_grid_row[i] = i % max_rows;
    }

    if (!desktop_load_ini()) {
        desktop_save_ini();
    }

    desktop_reload_program_apps();
    wm_mark_all_dirty();
}

static void desktop_delete_selected(void) {
    void *port = get_primary_drive_port();
    if (!port) return;

    int total_icons = 1 + g_desktop_file_count;
    for (int i = 1; i < total_icons; i++) {
        if (g_selected_icons[i]) {
            int file_idx = i - 1;
            if (file_idx >= 0 && file_idx < g_desktop_file_count) {
                char path[128]; int pos = 0;
                const char *prefix = "/Users/Desktop/";
                while (prefix[pos]) { path[pos] = prefix[pos]; pos++; }
                const char *name = g_desktop_files[file_idx].name;
                int k = 0; while (name[k] && pos < 120) path[pos++] = name[k++];
                path[pos] = '\0';

                fat32_delete_file(port, path);
                g_selected_icons[i] = 0;
            }
        }
    }
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
        int len = kstrlen(target_path);
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
        int len = kstrlen(path);
        fat32_write_file(port, "/VeloOS/System32/NOTEPAD_FILE.DAT", (void*)path, (UINT32)len);
        fat32_write_file(port, "/NOTEPAD_FILE.DAT", (void*)path, (UINT32)len);
    } else {
        fat32_delete_file(port, "/VeloOS/System32/NOTEPAD_FILE.DAT");
        fat32_delete_file(port, "/NOTEPAD_FILE.DAT");
    }
    task_spawn_app("NOTEPAD.BIN");
}

static void trigger_dock_bounce(int slot) {
    g_dock_bounce_slot = slot;
    g_dock_bounce_tick = 24;
    wm_mark_all_dirty();
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

static inline int boxes_intersect(int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2) {
    return !(x1 + w1 < x2 || x1 > x2 + w2 || y1 + h1 < y2 || y1 > y2 + h2);
}

#define PINNED_APP_COUNT 4

static void get_dock_layout(int *out_x, int *out_y, int *out_w, int *out_h, int *out_open_cnt) {
    int open_cnt = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        Window *win = wm_get_window(i);
        if (win && !win->is_closed) open_cnt++;
    }
    if (out_open_cnt) *out_open_cnt = open_cnt;

    int dock_h = 58;
    int slot_w = 50;
    
    int dock_w = 48 + (PINNED_APP_COUNT * slot_w) + (open_cnt > 0 ? (16 + open_cnt * slot_w) : 0) + 24;
    int dock_x = ((int)gop_width - dock_w) / 2;
    int dock_y = (int)gop_height - dock_h - 10;

    if (out_x) *out_x = dock_x;
    if (out_y) *out_y = dock_y;
    if (out_w) *out_w = dock_w;
    if (out_h) *out_h = dock_h;
}

static int get_dock_icon_magnification(int slot_center_x, int mouse_x, int mouse_y, int dock_y, int dock_h) {
    if (mouse_y < dock_y - 25 || mouse_y > dock_y + dock_h + 25) return 0;
    int dx = mouse_x - slot_center_x;
    if (dx < 0) dx = -dx;
    if (dx > 95) return 0;

    int boost = (14 * (95 - dx)) / 95;
    return (boost > 0) ? boost : 0;
}

static int get_dock_bounce_offset(int slot_idx) {
    if (slot_idx != g_dock_bounce_slot || g_dock_bounce_tick <= 0) return 0;
    static const int bounce_lut[24] = { 0, -4, -9, -15, -18, -20, -18, -15, -9, -4, 0, -3, -7, -11, -13, -11, -7, -3, 0, -2, -4, -2, 0, 0 };
    int idx = 24 - g_dock_bounce_tick;
    if (idx < 0) idx = 0;
    if (idx >= 24) idx = 23;
    return bounce_lut[idx];
}

static void update_marquee_selection(void) {
    int x1 = (g_marquee_sx < g_marquee_ex) ? g_marquee_sx : g_marquee_ex;
    int x2 = (g_marquee_sx < g_marquee_ex) ? g_marquee_ex : g_marquee_sx;
    int y1 = (g_marquee_sy < g_marquee_ey) ? g_marquee_sy : g_marquee_ey;
    int y2 = (g_marquee_sy < g_marquee_ey) ? g_marquee_ey : g_marquee_sy;
    int mw = x2 - x1;
    int mh = y2 - y1;

    int total_icons = 1 + g_desktop_file_count;

    for (int i = 0; i < total_icons; i++) {
        int col = g_icon_grid_col[i];
        int row = g_icon_grid_row[i];
        int ix = 20 + col * (DESKTOP_CELL_W + 12);
        int iy = 34 + row * DESKTOP_CELL_H;

        if (boxes_intersect(x1, y1, mw, mh, ix, iy, DESKTOP_CELL_W, DESKTOP_CELL_H)) {
            g_selected_icons[i] = 1;
        } else if (!keyboard_is_shift()) {
            g_selected_icons[i] = 0;
        }
    }
}

static int is_grid_occupied(int check_col, int check_row, int total_icons) {
    for (int k = 0; k < total_icons; k++) {
        if (!g_selected_icons[k]) {
            if (g_icon_grid_col[k] == check_col && g_icon_grid_row[k] == check_row) {
                return 1;
            }
        }
    }
    return 0;
}

static void handle_mouse_events(void) {
    MouseState *m = mouse_get_state();
    int mx = m->x, my = m->y;
    int cursor = CURSOR_ARROW;

    int dock_x, dock_y, dock_w, dock_h, open_apps;
    get_dock_layout(&dock_x, &dock_y, &dock_w, &dock_h, &open_apps);

    if (in_box(mx, my, dock_x, dock_y, dock_w, dock_h) || my <= 24) {
        cursor = CURSOR_HAND;
    } else if (start_menu_open && in_box(mx, my, dock_x, dock_y - 410, 410, 400)) {
        if (in_box(mx, my, dock_x + 12, dock_y - 48, 220, 26)) cursor = CURSOR_IBEAM;
        else cursor = CURSOR_HAND;
    }
    mouse_set_cursor(cursor);

    if (!m->left_button && wm_is_dragging()) wm_stop_drag();
    if (wm_is_dragging()) { wm_update_drag(mx, my); return; }

    /* Drag & Drop mit Kollisionsprüfung */
    if (g_drag_active && (!m->left_button || m->left_released)) {
        int delta_px_x = mx - g_drag_start_mx;
        int delta_px_y = my - g_drag_start_my;

        int delta_col = (delta_px_x >= 0) ? (delta_px_x + (DESKTOP_CELL_W / 2)) / (DESKTOP_CELL_W + 12)
                                          : (delta_px_x - (DESKTOP_CELL_W / 2)) / (DESKTOP_CELL_W + 12);
        int delta_row = (delta_px_y >= 0) ? (delta_px_y + (DESKTOP_CELL_H / 2)) / DESKTOP_CELL_H
                                          : (delta_px_y - (DESKTOP_CELL_H / 2)) / DESKTOP_CELL_H;

        int max_cols = ((int)gop_width - 40) / (DESKTOP_CELL_W + 12);
        int max_rows = ((int)gop_height - 90) / DESKTOP_CELL_H;
        int total_icons = 1 + g_desktop_file_count;

        int collision_detected = 0;

        for (int i = 0; i < total_icons; i++) {
            if (g_selected_icons[i]) {
                int nc = g_drag_origin_cols[i] + delta_col;
                int nr = g_drag_origin_rows[i] + delta_row;

                if (nc < 0 || nr < 0 || nc >= max_cols || nr >= max_rows || is_grid_occupied(nc, nr, total_icons)) {
                    collision_detected = 1;
                    break;
                }
            }
        }

        if (!collision_detected) {
            for (int i = 0; i < total_icons; i++) {
                if (g_selected_icons[i]) {
                    g_icon_grid_col[i] = g_drag_origin_cols[i] + delta_col;
                    g_icon_grid_row[i] = g_drag_origin_rows[i] + delta_row;
                }
            }
            desktop_save_ini();
        } else {
            g_snapback_active = 1;
            g_snapback_progress = 0;
            for (int i = 0; i < total_icons; i++) {
                if (g_selected_icons[i]) {
                    int orig_x = 20 + g_drag_origin_cols[i] * (DESKTOP_CELL_W + 12);
                    int orig_y = 34 + g_drag_origin_rows[i] * DESKTOP_CELL_H;
                    g_snapback_from_x[i] = orig_x + delta_px_x;
                    g_snapback_from_y[i] = orig_y + delta_px_y;
                }
            }
        }

        g_drag_active = 0;
        wm_mark_all_dirty();
        return;
    } else if (g_drag_active && m->left_button) {
        g_drag_cur_mx = mx;
        g_drag_cur_my = my;
        wm_mark_all_dirty();
        return;
    }

    if (g_marquee_active && (!m->left_button || m->left_released)) {
        g_marquee_active = 0;
        wm_mark_all_dirty();
    } else if (g_marquee_active && m->left_button) {
        g_marquee_ex = mx;
        g_marquee_ey = my;
        update_marquee_selection();
        wm_mark_all_dirty();
        return;
    }

    if (m->scroll_z != 0 || m->scroll_h != 0) {
        int target_win = -1;
        int local_x = 0, local_y = 0;

        for (int z = wm_get_z_count() - 1; z >= 0; z--) {
            int wid = wm_get_z_window(z);
            if (wid < 0) continue;
            Window *w = wm_get_window(wid);
            if (!w || w->is_closed || w->is_minimized) continue;

            if (in_box(mx, my, w->x, w->y, w->width, w->height)) {
                target_win = wid;
                int off_x = w->is_maximized ? 0 : 6;
                int off_y = 30;
                local_x = mx - (w->x + off_x);
                local_y = my - (w->y + off_y);
                break;
            }
        }

        if (target_win < 0) {
            int act = wm_get_active_window_id();
            if (act >= 0) {
                Window *w = wm_get_window(act);
                if (w && !w->is_closed && !w->is_minimized) {
                    target_win = act;
                    local_x = w->width / 2;
                    local_y = w->height / 2;
                }
            }
        }

        if (target_win >= 0) {
            wm_window_push_event(target_win, 5, local_x, local_y, 0, m->scroll_h, m->scroll_z);
            task_push_event(target_win, 5, local_x, local_y, 0, m->scroll_h, m->scroll_z);
        }

        m->scroll_z = 0;
        m->scroll_h = 0;
    }

    for (int z = wm_get_z_count() - 1; z >= 0; z--) {
        int i = wm_get_z_window(z);
        if (i < 0) continue;
        Window *win = wm_get_window(i);
        if (!win || win->is_closed || win->is_minimized) continue;

        if (in_box(mx, my, win->x, win->y, win->width, win->height)) {
            if (start_menu_open) {
                start_menu_open = 0;
                g_search_focused = 0;
                wm_mark_all_dirty();
            }

            int btn_top = win->y + 5;
            int btn_h = 20;

            if (m->left_clicked && in_box(mx, my, win->x + win->width - 26, btn_top, 16, btn_h)) {
                m->left_clicked = 0; wm_close_window(i); return;
            } else if (m->left_clicked && in_box(mx, my, win->x + win->width - 46, btn_top, 16, btn_h)) {
                m->left_clicked = 0; wm_maximize_window(i); return;
            } else if (m->left_clicked && in_box(mx, my, win->x + win->width - 66, btn_top, 16, btn_h)) {
                m->left_clicked = 0; wm_minimize_window_to(i, dock_x + dock_w / 2, dock_y); return;
            }

            if (m->left_clicked || m->right_clicked) wm_focus_window(i);

            if (m->left_clicked && in_box(mx, my, win->x, win->y, win->width - 70, 30)) {
                m->left_clicked = 0; wm_start_drag(i, mx, my); return;
            }

            int off_x = win->is_maximized ? 0 : 6;
            int off_y = 30;
            int local_x = mx - (win->x + off_x);
            int local_y = my - (win->y + off_y);

            if (m->left_clicked) {
                m->left_clicked = 0;
                wm_window_push_event(i, 1, local_x, local_y, 0, 0, 0);
                task_push_event(i, 1, local_x, local_y, 0, 0, 0);
            } else if (m->right_clicked) {
                m->right_clicked = 0;
                wm_window_push_event(i, 4, local_x, local_y, 0, 0, 0);
                task_push_event(i, 4, local_x, local_y, 0, 0, 0);
            }
            return;
        }
    }

    if (m->right_clicked && my > 24 && !in_box(mx, my, dock_x, dock_y, dock_w, dock_h)) {
        m->right_clicked = 0;
        start_menu_open = 0;
        g_search_focused = 0;
        g_ctx_menu_open = 1;
        g_ctx_menu_x = (mx + CTX_MENU_W > (int)gop_width) ? (int)gop_width - CTX_MENU_W - 4 : mx;
        g_ctx_menu_y = (my + CTX_MENU_H > (int)gop_height - 70) ? (int)gop_height - CTX_MENU_H - 70 : my;
        wm_mark_all_dirty();
        return;
    }

    if (!m->left_clicked && !m->left_button) return;

    if (g_ctx_menu_open && m->left_clicked) {
        m->left_clicked = 0;
        if (in_box(mx, my, g_ctx_menu_x, g_ctx_menu_y, CTX_MENU_W, CTX_MENU_H)) {
            int item_idx = (my - (g_ctx_menu_y + 6)) / 32;
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

    if (in_box(mx, my, dock_x, dock_y, dock_w, dock_h) && m->left_clicked) {
        m->left_clicked = 0;

        int cur_x = dock_x + 12;

        if (in_box(mx, my, cur_x, dock_y + 4, 44, 50)) { trigger_dock_bounce(0); launch_explorer_at_path("Computer"); return; }
        cur_x += 50;

        if (in_box(mx, my, cur_x, dock_y + 4, 44, 50)) { trigger_dock_bounce(1); task_spawn_app("BROWSER.BIN"); return; }
        cur_x += 50;

        if (in_box(mx, my, cur_x, dock_y + 4, 44, 50)) { trigger_dock_bounce(2); task_spawn_app("NOTEPAD.BIN"); return; }
        cur_x += 50;

        if (in_box(mx, my, cur_x, dock_y + 4, 44, 50)) { trigger_dock_bounce(3); task_spawn_app("SH.BIN"); return; }
        cur_x += 50;

        if (in_box(mx, my, cur_x, dock_y + 4, 44, 50)) {
            trigger_dock_bounce(4);
            start_menu_open = !start_menu_open;
            start_menu_selected = 0;
            g_search_focused = 0;
            g_ctx_menu_open = 0;
            if (start_menu_open) desktop_reload_program_apps();
            wm_mark_all_dirty();
            return;
        }
        cur_x += 50;

        cur_x += 16;

        for (int i = 0; i < MAX_WINDOWS; i++) {
            Window *win = wm_get_window(i);
            if (!win || win->is_closed) continue;

            if (in_box(mx, my, cur_x, dock_y + 4, 44, 50)) {
                if (win->is_minimized) wm_minimize_window_to(i, cur_x + 20, dock_y);
                else if (win->is_active) wm_minimize_window_to(i, cur_x + 20, dock_y);
                else wm_focus_window(i);
                return;
            }
            cur_x += 50;
        }
        return;
    }

    if (start_menu_open && m->left_clicked) {
        int menu_w = 410, menu_h = 400;
        int menu_x = dock_x;
        int menu_y = dock_y - menu_h - 12;
        if (menu_x + menu_w > (int)gop_width - 8) menu_x = (int)gop_width - menu_w - 8;
        if (menu_x < 8) menu_x = 8;

        if (mx >= menu_x && mx <= menu_x + menu_w && my >= menu_y && my <= menu_y + menu_h) {
            if (my >= menu_y + menu_h - 38 && mx >= menu_x + menu_w - 80) {
                m->left_clicked = 0; system_shutdown(); return;
            }
            if (my >= menu_y + menu_h - 38 && mx >= menu_x + menu_w - 165 && mx < menu_x + menu_w - 80) {
                m->left_clicked = 0; system_reboot(); return;
            }

            if (mx >= menu_x + 10 && mx <= menu_x + 230 && my >= menu_y + menu_h - 38 && my <= menu_y + menu_h - 10) {
                m->left_clicked = 0;
                g_search_focused = 1;
                wm_mark_all_dirty();
                return;
            } else {
                g_search_focused = 0;
            }

            int item_y = menu_y + 14;
            for (int i = 0; i < g_program_app_count; i++) {
                if (str_contains_nocase(g_program_apps[i].name, g_search_box.text)) {
                    if (mx <= menu_x + 240 && my >= item_y && my <= item_y + 34) {
                        m->left_clicked = 0;
                        execute_app_action(i);
                        return;
                    }
                    item_y += 36;
                    if (item_y > menu_y + menu_h - 55) break;
                }
            }

            if (mx >= menu_x + 244 && mx <= menu_x + menu_w) {
                if (my >= menu_y + 54 && my <= menu_y + 80) {
                    m->left_clicked = 0; launch_explorer_at_path("C:/Users/Documents"); start_menu_open = 0; wm_mark_all_dirty(); return;
                } else if (my >= menu_y + 82 && my <= menu_y + 108) {
                    m->left_clicked = 0; launch_explorer_at_path("C:/Users/Pictures"); start_menu_open = 0; wm_mark_all_dirty(); return;
                } else if (my >= menu_y + 110 && my <= menu_y + 136) {
                    m->left_clicked = 0; launch_explorer_at_path("C:/Users/Downloads"); start_menu_open = 0; wm_mark_all_dirty(); return;
                } else if (my >= menu_y + 138 && my <= menu_y + 164) {
                    m->left_clicked = 0; launch_explorer_at_path("C:/Programs"); start_menu_open = 0; wm_mark_all_dirty(); return;
                } else if (my >= menu_y + 166 && my <= menu_y + 192) {
                    m->left_clicked = 0; launch_explorer_at_path("Computer"); start_menu_open = 0; wm_mark_all_dirty(); return;
                }
            }
            return;
        } else {
            m->left_clicked = 0; start_menu_open = 0; g_search_focused = 0; wm_mark_all_dirty();
        }
    }

    int total_icons = 1 + g_desktop_file_count;
    int clicked_icon = -1;

    for (int i = 0; i < total_icons; i++) {
        int col = g_icon_grid_col[i];
        int row = g_icon_grid_row[i];
        int ix = 20 + col * (DESKTOP_CELL_W + 12);
        int iy = 34 + row * DESKTOP_CELL_H;

        if (in_box(mx, my, ix, iy, DESKTOP_CELL_W, DESKTOP_CELL_H)) {
            clicked_icon = i;
            break;
        }
    }

    if (clicked_icon >= 0) {
        if (m->left_clicked) {
            m->left_clicked = 0;
            int is_shift = keyboard_is_shift();

            if (is_shift) {
                g_selected_icons[clicked_icon] = !g_selected_icons[clicked_icon];
            } else {
                if (g_selected_icons[clicked_icon] && g_last_click_icon == clicked_icon) {
                    open_desktop_item(clicked_icon);
                    g_last_click_icon = -1;
                    wm_mark_all_dirty();
                    return;
                }
                if (!g_selected_icons[clicked_icon]) {
                    for (int k = 0; k <= MAX_DESKTOP_ICONS; k++) g_selected_icons[k] = 0;
                    g_selected_icons[clicked_icon] = 1;
                }
            }
            g_last_click_icon = clicked_icon;

            g_drag_active = 1;
            g_drag_start_mx = mx;
            g_drag_start_my = my;
            g_drag_cur_mx = mx;
            g_drag_cur_my = my;

            for (int k = 0; k < total_icons; k++) {
                g_drag_origin_cols[k] = g_icon_grid_col[k];
                g_drag_origin_rows[k] = g_icon_grid_row[k];
            }

            wm_mark_all_dirty();
        }
    } else if (m->left_clicked) {
        if (!keyboard_is_shift()) {
            for (int k = 0; k <= MAX_DESKTOP_ICONS; k++) g_selected_icons[k] = 0;
        }
        g_last_click_icon = -1;
        g_marquee_active = 1;
        g_marquee_sx = mx;
        g_marquee_sy = my;
        g_marquee_ex = mx;
        g_marquee_ey = my;
        wm_mark_all_dirty();
    }
}

static void render_desktop_background(void) {
    UINTN bg_bytes = (UINTN)((const char*)&g_wallpaper_end[0] - (const char*)&g_wallpaper_start[0]);
    UINTN total_pixels = bg_bytes / 4;

    if (total_pixels > 0 && (const void*)g_wallpaper_start != NULL && ((UINTN)&g_wallpaper_end[0] > (UINTN)&g_wallpaper_start[0])) {
        const UINT32 *src = (const UINT32*)(const void*)&g_wallpaper_start[0];
        
        UINTN src_w = gop_width;
        UINTN src_h = gop_height;

        if (total_pixels == 1920 * 1080) { src_w = 1920; src_h = 1080; }
        else if (total_pixels == 1024 * 768) { src_w = 1024; src_h = 768; }
        else if (total_pixels == 1280 * 720) { src_w = 1280; src_h = 720; }
        else if (total_pixels == 1280 * 800) { src_w = 1280; src_h = 800; }
        else if (total_pixels == 1280 * 1024) { src_w = 1280; src_h = 1024; }
        else if (total_pixels == 1366 * 768) { src_w = 1366; src_h = 768; }
        else if (total_pixels == 1440 * 900) { src_w = 1440; src_h = 900; }
        else if (total_pixels == 1600 * 900) { src_w = 1600; src_h = 900; }
        else if (total_pixels == 1680 * 1050) { src_w = 1680; src_h = 1050; }
        else if (total_pixels == 2560 * 1440) { src_w = 2560; src_h = 1440; }
        else if (total_pixels == 3840 * 2160) { src_w = 3840; src_h = 2160; }

        if (src_w == gop_width && src_h == gop_height) {
            for (UINTN y = 0; y < gop_height; y++) {
                __builtin_memcpy(&g_backbuffer[y * gop_width], &src[y * src_w], gop_width * sizeof(UINT32));
            }
        } else {
            for (UINTN y = 0; y < gop_height; y++) {
                UINTN sy = (y * src_h) / gop_height;
                const UINT32 *src_row = &src[sy * src_w];
                UINT32 *dst_row = &g_backbuffer[y * gop_width];

                for (UINTN x = 0; x < gop_width; x++) {
                    UINTN sx = (x * src_w) / gop_width;
                    dst_row[x] = src_row[sx];
                }
            }
        }
    } else {
        draw_rounded_rect_gradient(0, 0, (int)gop_width, (int)gop_height, 0, 0x000F172A, 0x00020617);
    }
}

void desktop_start(void) {
    start_menu_open = 0; start_menu_selected = 0;
    g_search_focused = 0; g_ctx_menu_open = 0;
    g_last_click_icon = -1;
    g_marquee_active = 0;
    g_drag_active = 0;
    g_snapback_active = 0;
    g_dock_bounce_slot = -1;
    for (int i = 0; i <= MAX_DESKTOP_ICONS; i++) g_selected_icons[i] = 0;

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

    int anim_active = wm_tick_animations();

    if (g_dock_bounce_tick > 0) {
        g_dock_bounce_tick--;
        if (g_dock_bounce_tick == 0) g_dock_bounce_slot = -1;
        wm_mark_all_dirty();
    }

    if (g_snapback_active) {
        g_snapback_progress += 25;
        if (g_snapback_progress >= 100) {
            g_snapback_active = 0;
        }
        wm_mark_all_dirty();
    }

    if (mouse_moved || m->left_clicked || m->right_clicked || m->scroll_z != 0 || wm_is_dragging() || g_marquee_active || g_drag_active || anim_active || g_snapback_active) {
        handle_mouse_events();
        wm_mark_all_dirty();
    }

    int day = 1, month = 1, year = 2026, raw_h = 12, m_t = 0, s = 0;
    get_rtc_time(&day, &month, &year, &raw_h, &m_t, &s);
    int offset = is_dst_european(year, month, day, raw_h) ? 2 : 1;
    const char *tz_str = (offset == 2) ? "MESZ" : "MEZ";
    int h = (raw_h + offset + 24) % 24;

    if (s != last_second) {
        last_second = s;
        wm_mark_all_dirty();
    }

    if (!wm_is_dirty()) return;

    // 1. Hintergrund zeichnen
    render_desktop_background();

    // 2. Desktop-Grid mit Desktop-Icons
    int total_icons = 1 + g_desktop_file_count;

    for (int i = 0; i < total_icons; i++) {
        int col = g_icon_grid_col[i];
        int row = g_icon_grid_row[i];
        int ix = 20 + col * (DESKTOP_CELL_W + 12);
        int iy = 34 + row * DESKTOP_CELL_H;

        if (g_selected_icons[i]) {
            draw_rounded_rect_aa(ix, iy, DESKTOP_CELL_W, DESKTOP_CELL_H, 8, 0x000284C7);
            draw_rounded_rect_aa(ix + 1, iy + 1, DESKTOP_CELL_W - 2, DESKTOP_CELL_H - 2, 7, 0x0038BDF8);
        }

        const char *name_str = "Computer";
        int icon_x = ix + (DESKTOP_CELL_W - 32) / 2;
        int icon_y = iy + 8;

        draw_rounded_rect_aa(icon_x + 2, icon_y + 3, 32, 32, 6, 0x00010810);

        if (i == 0) {
            name_str = "Computer";
            render_icon_pc_32(icon_x, icon_y, desktop_icon_set_pixel);
        } else {
            int file_idx = i - 1;
            if (file_idx >= 0 && file_idx < g_desktop_file_count) {
                VeloDirEntry *cur = &g_desktop_files[file_idx];
                name_str = cur->name;
                if (cur->is_dir) render_icon_folder_32(icon_x, icon_y, desktop_icon_set_pixel);
                else if (is_executable(cur->name)) render_icon_app_32(icon_x, icon_y, desktop_icon_set_pixel);
                else if (str_ends_with_nocase(cur->name, ".txt") || str_ends_with_nocase(cur->name, ".dat"))
                    render_icon_doc_32(icon_x, icon_y, desktop_icon_set_pixel);
                else render_icon_file_32(icon_x, icon_y, desktop_icon_set_pixel);
            }
        }

        char formatted_name[24];
        format_desktop_filename(name_str, formatted_name, 10);

        int text_len = kstrlen(formatted_name);
        int text_px_w = text_len * 8;
        int tx = ix + (DESKTOP_CELL_W - text_px_w) / 2;
        if (tx < ix + 2) tx = ix + 2;

        UINT32 bg_under = get_pixel((UINTN)(ix + DESKTOP_CELL_W / 2), (UINTN)(iy + 48));
        UINT32 text_col = wm_get_contrast_color(bg_under);
        UINT32 shadow_col = (text_col == 0x00F8FAFC) ? 0x00000000 : 0x00FFFFFF;

        wm_draw_text_shadow(formatted_name, tx, iy + 48, 1, text_col, shadow_col);
    }

    // 3. ECHTE MULTI-DRAG & ELASTISCHE SNAP-BACK ANIMATION
    if (g_drag_active || g_snapback_active) {
        int delta_px_x = g_drag_cur_mx - g_drag_start_mx;
        int delta_px_y = g_drag_cur_my - g_drag_start_my;

        for (int i = 0; i < total_icons; i++) {
            if (g_selected_icons[i]) {
                int orig_ix = 20 + g_drag_origin_cols[i] * (DESKTOP_CELL_W + 12);
                int orig_iy = 34 + g_drag_origin_rows[i] * DESKTOP_CELL_H;

                int drag_x = orig_ix + delta_px_x;
                int drag_y = orig_iy + delta_px_y;

                if (g_snapback_active) {
                    int p = g_snapback_progress;
                    drag_x = g_snapback_from_x[i] + ((orig_ix - g_snapback_from_x[i]) * p) / 100;
                    drag_y = g_snapback_from_y[i] + ((orig_iy - g_snapback_from_y[i]) * p) / 100;
                }

                draw_rounded_rect_aa(drag_x - 2, drag_y - 2, DESKTOP_CELL_W + 4, DESKTOP_CELL_H + 4, 8, 0x0038BDF8);
                draw_frosted_glass_rect(drag_x, drag_y, DESKTOP_CELL_W, DESKTOP_CELL_H, 8, 0x000284C7, 130);

                int icon_x = drag_x + (DESKTOP_CELL_W - 32) / 2;
                int icon_y = drag_y + 8;

                if (i == 0) render_icon_pc_32(icon_x, icon_y, desktop_icon_set_pixel);
                else {
                    int file_idx = i - 1;
                    if (file_idx >= 0 && file_idx < g_desktop_file_count) {
                        VeloDirEntry *cur = &g_desktop_files[file_idx];
                        if (cur->is_dir) render_icon_folder_32(icon_x, icon_y, desktop_icon_set_pixel);
                        else if (is_executable(cur->name)) render_icon_app_32(icon_x, icon_y, desktop_icon_set_pixel);
                        else render_icon_doc_32(icon_x, icon_y, desktop_icon_set_pixel);
                    }
                }
            }
        }
    }

    // 4. Alle Fenster rendern
    wm_render_all();

    // 5. Menüleiste oben
    draw_frosted_glass_rect(0, 0, (int)gop_width, 24, 0, 0x001E293B, 180);
    draw_filled_rect(0, 23, gop_width, 1, 0x00334155);

    wm_draw_text("VeloOS", 14, 4, 0x0038BDF8, 0x00000000);
    
    int act_id = wm_get_active_window_id();
    if (act_id >= 0) {
        Window *awin = wm_get_window(act_id);
        if (awin && !awin->is_closed) {
            wm_draw_text("|", 70, 4, 0x0064748B, 0x00000000);
            wm_draw_text(awin->title, 82, 4, 0x00F8FAFC, 0x00000000);
        }
    }

    char date_time_str[48];
    date_time_str[0] = '0' + (char)(day / 10);
    date_time_str[1] = '0' + (char)(day % 10);
    date_time_str[2] = '.';
    date_time_str[3] = '0' + (char)(month / 10);
    date_time_str[4] = '0' + (char)(month % 10);
    date_time_str[5] = '.';
    date_time_str[6] = '0' + (char)((year / 1000) % 10);
    date_time_str[7] = '0' + (char)((year / 100) % 10);
    date_time_str[8] = '0' + (char)((year / 10) % 10);
    date_time_str[9] = '0' + (char)(year % 10);
    date_time_str[10] = ' ';
    date_time_str[11] = ' ';
    date_time_str[12] = '0' + (char)(h / 10);
    date_time_str[13] = '0' + (char)(h % 10);
    date_time_str[14] = ':';
    date_time_str[15] = '0' + (char)(m_t / 10);
    date_time_str[16] = '0' + (char)(m_t % 10);
    date_time_str[17] = ':';
    date_time_str[18] = '0' + (char)(s / 10);
    date_time_str[19] = '0' + (char)(s % 10);
    date_time_str[20] = ' ';
    date_time_str[21] = tz_str[0];
    date_time_str[22] = tz_str[1];
    date_time_str[23] = tz_str[2];
    date_time_str[24] = tz_str[3];
    date_time_str[25] = '\0';

    int pill_w = 260;
    int pill_x = (int)gop_width - pill_w - 10;
    draw_rounded_rect_gradient(pill_x, 2, pill_w, 20, 10, 0x001E293B, 0x000F172A);
    draw_rounded_rect_aa(pill_x, 2, pill_w, 20, 10, 0x00334155);
    wm_draw_text(date_time_str, pill_x + 10, 4, 0x00CBD5E1, 0x00000000);

    // 6. Großes schwebendes Dock unten mit hüpfenden Icons (macOS Bounce)
    int dock_x, dock_y, dock_w, dock_h, open_apps;
    get_dock_layout(&dock_x, &dock_y, &dock_w, &dock_h, &open_apps);

    draw_frosted_glass_rect(dock_x, dock_y, dock_w, dock_h, 24, 0x00FFFFFF, 45);
    draw_rounded_rect_aa(dock_x, dock_y, dock_w, dock_h, 24, 0x005AC0E0);

    int cur_slot_x = dock_x + 12;

    int mag0 = get_dock_icon_magnification(cur_slot_x + 20, m->x, m->y, dock_y, dock_h);
    int bnc0 = get_dock_bounce_offset(0);
    int slot_size0 = 40 + mag0;
    int slot_y0 = dock_y + (dock_h - slot_size0) / 2 - (mag0 / 2) + bnc0;

    draw_rounded_rect_gradient(cur_slot_x - mag0 / 2, slot_y0, slot_size0, slot_size0, 12 + mag0 / 2, start_menu_open ? 0x000284C7 : 0x001E293B, 0x000F172A);
    draw_rounded_rect_aa(cur_slot_x - mag0 / 2, slot_y0, slot_size0, slot_size0, 12 + mag0 / 2, start_menu_open ? 0x0038BDF8 : 0x00334155);
    render_svg_app(cur_slot_x + 8 - mag0 / 4, slot_y0 + 8, 24 + mag0, desktop_icon_set_pixel);
    cur_slot_x += 48;

    int mag1 = get_dock_icon_magnification(cur_slot_x + 20, m->x, m->y, dock_y, dock_h);
    int bnc1 = get_dock_bounce_offset(1);
    int slot_size1 = 40 + mag1;
    int slot_y1 = dock_y + (dock_h - slot_size1) / 2 - (mag1 / 2) + bnc1;

    draw_rounded_rect_gradient(cur_slot_x - mag1 / 2, slot_y1, slot_size1, slot_size1, 12 + mag1 / 2, 0x001E293B, 0x000F172A);
    draw_rounded_rect_aa(cur_slot_x - mag1 / 2, slot_y1, slot_size1, slot_size1, 12 + mag1 / 2, 0x00334155);
    render_svg_folder(cur_slot_x + 8 - mag1 / 4, slot_y1 + 8, 24 + mag1, desktop_icon_set_pixel);
    cur_slot_x += 48;

    int mag2 = get_dock_icon_magnification(cur_slot_x + 20, m->x, m->y, dock_y, dock_h);
    int bnc2 = get_dock_bounce_offset(2);
    int slot_size2 = 40 + mag2;
    int slot_y2 = dock_y + (dock_h - slot_size2) / 2 - (mag2 / 2) + bnc2;

    draw_rounded_rect_gradient(cur_slot_x - mag2 / 2, slot_y2, slot_size2, slot_size2, 12 + mag2 / 2, 0x001E293B, 0x000F172A);
    draw_rounded_rect_aa(cur_slot_x - mag2 / 2, slot_y2, slot_size2, slot_size2, 12 + mag2 / 2, 0x00334155);
    render_svg_app(cur_slot_x + 8 - mag2 / 4, slot_y2 + 8, 24 + mag2, desktop_icon_set_pixel);
    cur_slot_x += 48;

    int mag3 = get_dock_icon_magnification(cur_slot_x + 20, m->x, m->y, dock_y, dock_h);
    int bnc3 = get_dock_bounce_offset(3);
    int slot_size3 = 40 + mag3;
    int slot_y3 = dock_y + (dock_h - slot_size3) / 2 - (mag3 / 2) + bnc3;

    draw_rounded_rect_gradient(cur_slot_x - mag3 / 2, slot_y3, slot_size3, slot_size3, 12 + mag3 / 2, 0x001E293B, 0x000F172A);
    draw_rounded_rect_aa(cur_slot_x - mag3 / 2, slot_y3, slot_size3, slot_size3, 12 + mag3 / 2, 0x00334155);
    render_svg_doc(cur_slot_x + 8 - mag3 / 4, slot_y3 + 8, 24 + mag3, desktop_icon_set_pixel);
    cur_slot_x += 48;

    int mag4 = get_dock_icon_magnification(cur_slot_x + 20, m->x, m->y, dock_y, dock_h);
    int bnc4 = get_dock_bounce_offset(4);
    int slot_size4 = 40 + mag4;
    int slot_y4 = dock_y + (dock_h - slot_size4) / 2 - (mag4 / 2) + bnc4;

    draw_rounded_rect_gradient(cur_slot_x - mag4 / 2, slot_y4, slot_size4, slot_size4, 12 + mag4 / 2, 0x001E293B, 0x000F172A);
    draw_rounded_rect_aa(cur_slot_x - mag4 / 2, slot_y4, slot_size4, slot_size4, 12 + mag4 / 2, 0x00334155);
    render_svg_app(cur_slot_x + 8 - mag4 / 4, slot_y4 + 8, 24 + mag4, desktop_icon_set_pixel);
    cur_slot_x += 48;

    if (open_apps > 0) {
        draw_filled_rect((UINTN)(cur_slot_x + 6), (UINTN)(dock_y + 12), 1, (UINTN)(dock_h - 24), 0x00334155);
        cur_slot_x += 16;

        for (int i = 0; i < MAX_WINDOWS; i++) {
            Window *win = wm_get_window(i);
            if (!win || win->is_closed) continue;

            int is_act = win->is_active && !win->is_minimized;
            int magW = get_dock_icon_magnification(cur_slot_x + 20, m->x, m->y, dock_y, dock_h);
            int slot_sizeW = 40 + magW;
            int slot_yW = dock_y + (dock_h - slot_sizeW) / 2 - (magW / 2);

            if (is_act) {
                draw_rounded_rect_gradient(cur_slot_x - magW / 2, slot_yW, slot_sizeW, slot_sizeW, 12 + magW / 2, 0x000284C7, 0x000369A1);
                draw_rounded_rect_aa(cur_slot_x - magW / 2, slot_yW, slot_sizeW, slot_sizeW, 12 + magW / 2, 0x0038BDF8);
                render_svg_app(cur_slot_x + 8 - magW / 4, slot_yW + 8, 24 + magW, desktop_icon_set_pixel);
                draw_rounded_rect_aa(cur_slot_x + 18, dock_y + dock_h - 6, 4, 3, 1, 0x0038BDF8);
            } else {
                draw_rounded_rect_gradient(cur_slot_x - magW / 2, slot_yW, slot_sizeW, slot_sizeW, 12 + magW / 2, 0x000F172A, 0x001E293B);
                draw_rounded_rect_aa(cur_slot_x - magW / 2, slot_yW, slot_sizeW, slot_sizeW, 12 + magW / 2, 0x00334155);
                render_svg_app(cur_slot_x + 8 - magW / 4, slot_yW + 8, 24 + magW, desktop_icon_set_pixel);
                if (!win->is_minimized) {
                    draw_rounded_rect_aa(cur_slot_x + 18, dock_y + dock_h - 6, 4, 3, 1, 0x0064748B);
                }
            }

            cur_slot_x += 48;
        }
    }

    // 7. Gummiband-Auswahlrechteck
    if (g_marquee_active) {
        int x1 = (g_marquee_sx < g_marquee_ex) ? g_marquee_sx : g_marquee_ex;
        int x2 = (g_marquee_sx < g_marquee_ex) ? g_marquee_ex : g_marquee_sx;
        int y1 = (g_marquee_sy < g_marquee_ey) ? g_marquee_sy : g_marquee_ey;
        int y2 = (g_marquee_sy < g_marquee_ey) ? g_marquee_ey : g_marquee_sy;

        for (int py = y1; py < y2; py++) {
            if (py < 0 || py >= (int)gop_height) continue;
            for (int px = x1; px < x2; px++) {
                if (px < 0 || px >= (int)gop_width) continue;
                UINT32 bg = get_pixel((UINTN)px, (UINTN)py);
                put_pixel((UINTN)px, (UINTN)py, alpha_blend(0x0038BDF8, bg, 55));
            }
        }
        for (int px = x1; px < x2; px++) {
            if (px >= 0 && px < (int)gop_width) {
                if (y1 >= 0 && y1 < (int)gop_height) put_pixel((UINTN)px, (UINTN)y1, 0x0038BDF8);
                if (y2 - 1 >= 0 && y2 - 1 < (int)gop_height) put_pixel((UINTN)px, (UINTN)(y2 - 1), 0x0038BDF8);
            }
        }
        for (int py = y1; py < y2; py++) {
            if (py >= 0 && py < (int)gop_height) {
                if (x1 >= 0 && x1 < (int)gop_width) put_pixel((UINTN)x1, (UINTN)py, 0x0038BDF8);
                if (x2 - 1 >= 0 && x2 - 1 < (int)gop_width) put_pixel((UINTN)(x2 - 1), (UINTN)py, 0x0038BDF8);
            }
        }
    }

    // 8. Start-Popup
    if (start_menu_open) {
        int menu_w = 410, menu_h = 400;
        int menu_x = dock_x;
        int menu_y = dock_y - menu_h - 12;
        if (menu_x + menu_w > (int)gop_width - 8) menu_x = (int)gop_width - menu_w - 8;
        if (menu_x < 8) menu_x = 8;

        draw_rounded_rect_aa(menu_x - 1, menu_y - 1, menu_w + 2, menu_h + 2, 14, 0x00020617);
        draw_frosted_glass_rect(menu_x, menu_y, menu_w, menu_h, 13, 0x001E293B, 220);
        draw_rounded_rect_aa(menu_x, menu_y, menu_w, menu_h, 13, 0x0038BDF8);

        draw_rounded_rect_gradient(menu_x + 8, menu_y + 8, 234, menu_h - 56, 6, 0x000F172A, 0x00020617);
        draw_rounded_rect_aa(menu_x + 8, menu_y + 8, 234, menu_h - 56, 6, 0x00334155);

        int item_y = menu_y + 14, visible_idx = 0;
        for (int i = 0; i < g_program_app_count; i++) {
            if (str_contains_nocase(g_program_apps[i].name, g_search_box.text)) {
                if (visible_idx == start_menu_selected) {
                    draw_rounded_rect_gradient(menu_x + 12, item_y - 2, 226, 32, 4, 0x000284C7, 0x000369A1);
                    draw_rounded_rect_aa(menu_x + 12, item_y - 2, 226, 32, 4, 0x0038BDF8);
                    render_svg_app(menu_x + 16, item_y + 2, 24, desktop_icon_set_pixel);
                    wm_draw_text(g_program_apps[i].name, menu_x + 46, item_y + 6, 0x00FFFFFF, 0x00000000);
                } else {
                    render_svg_app(menu_x + 16, item_y + 2, 24, desktop_icon_set_pixel);
                    wm_draw_text(g_program_apps[i].name, menu_x + 46, item_y + 6, 0x00E2E8F0, 0x00000000);
                }
                item_y += 36;
                visible_idx++;
                if (item_y > menu_y + menu_h - 60) break;
            }
        }

        draw_rounded_rect_aa(menu_x + 252, menu_y + 14, 28, 28, 14, 0x0038BDF8);
        draw_rounded_rect_gradient(menu_x + 254, menu_y + 16, 24, 24, 12, 0x000284C7, 0x000369A1);
        wm_draw_text(g_user_name_active, menu_x + 288, menu_y + 20, 0x00FFFFFF, 0x00000000);

        draw_filled_rect((UINTN)(menu_x + 248), (UINTN)(menu_y + 48), 154, 1, 0x00334155);

        wm_draw_text("Dokumente", menu_x + 258, menu_y + 60, 0x0094A3B8, 0x00000000);
        wm_draw_text("Bilder", menu_x + 258, menu_y + 88, 0x0094A3B8, 0x00000000);
        wm_draw_text("Downloads", menu_x + 258, menu_y + 116, 0x0094A3B8, 0x00000000);
        wm_draw_text("Programme", menu_x + 258, menu_y + 144, 0x0094A3B8, 0x00000000);
        wm_draw_text("Computer", menu_x + 258, menu_y + 172, 0x0038BDF8, 0x00000000);

        UINT32 s_border = g_search_focused ? 0x0038BDF8 : 0x00334155;
        draw_rounded_rect_aa(menu_x + 10, menu_y + menu_h - 38, 220, 26, 4, s_border);
        draw_rounded_rect_gradient(menu_x + 11, menu_y + menu_h - 37, 218, 24, 3, 0x00020617, 0x000F172A);

        if (g_search_box.len > 0) {
            char visible_buf[32];
            int v_len = g_search_box.len - g_search_box.scroll_offset;
            if (v_len > g_search_box.max_visible) v_len = g_search_box.max_visible;
            for (int k = 0; k < v_len; k++) visible_buf[k] = g_search_box.text[g_search_box.scroll_offset + k];
            visible_buf[v_len] = '\0';
            wm_draw_text(visible_buf, menu_x + 18, menu_y + menu_h - 30, 0x00FFFFFF, 0x00000000);
        } else {
            wm_draw_text("Suchen...", menu_x + 18, menu_y + menu_h - 30, 0x0064748B, 0x00000000);
        }

        draw_rounded_rect_gradient(menu_x + menu_w - 165, menu_y + menu_h - 38, 75, 26, 4, 0x001E293B, 0x000F172A);
        draw_rounded_rect_aa(menu_x + menu_w - 165, menu_y + menu_h - 38, 75, 26, 4, 0x0038BDF8);
        wm_draw_text("Reboot", menu_x + menu_w - 152, menu_y + menu_h - 30, 0x00FFFFFF, 0x00000000);

        draw_rounded_rect_gradient(menu_x + menu_w - 80, menu_y + menu_h - 38, 70, 26, 4, 0x007F1D1D, 0x00450A0A);
        draw_rounded_rect_aa(menu_x + menu_w - 80, menu_y + menu_h - 38, 70, 26, 4, 0x00EF4444);
        wm_draw_text("Power", menu_x + menu_w - 68, menu_y + menu_h - 30, 0x00FFFFFF, 0x00000000);
    }

    // 9. Kontextmenü
    if (g_ctx_menu_open) {
        int cx = g_ctx_menu_x, cy = g_ctx_menu_y;
        draw_rounded_rect_aa(cx + 2, cy + 2, CTX_MENU_W, CTX_MENU_H, 6, 0x00020617);
        draw_frosted_glass_rect(cx, cy, CTX_MENU_W, CTX_MENU_H, 5, 0x001E293B, 230);
        draw_rounded_rect_aa(cx, cy, CTX_MENU_W, CTX_MENU_H, 5, 0x00334155);

        int item_y = cy + 6;
        for (int i = 0; i < CTX_ITEM_COUNT; i++) {
            if (in_box(m->x, m->y, cx + 2, item_y, CTX_MENU_W - 4, 30)) {
                draw_rounded_rect_gradient(cx + 4, item_y, CTX_MENU_W - 8, 30, 4, 0x000284C7, 0x000369A1);
                draw_rounded_rect_aa(cx + 4, item_y, CTX_MENU_W - 8, 30, 4, 0x0038BDF8);
                wm_draw_text(g_ctx_labels[i], cx + 14, item_y + 7, 0x00FFFFFF, 0x00000000);
            } else {
                wm_draw_text(g_ctx_labels[i], cx + 14, item_y + 7, 0x00CBD5E1, 0x00000000);
            }
            item_y += 32;
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
                wm_window_push_event(active_id, 2, 0, 0, c, 0, 0);
                task_push_event(active_id, 2, 0, 0, c, 0, 0);
            }
        }
    } else {
        if (c == KEY_DELETE || (unsigned char)c == 0x88 || (unsigned char)c == 0x7F) {
            desktop_delete_selected();
        } else if (c == '\n' || c == '\r') {
            int total_icons = 1 + g_desktop_file_count;
            for (int i = 0; i < total_icons; i++) {
                if (g_selected_icons[i]) {
                    open_desktop_item(i);
                    break;
                }
            }
        }
    }
}
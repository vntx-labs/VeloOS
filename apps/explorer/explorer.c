#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <velo/window.h>
#include <velo/syscall.h>
#include <velo/net.h>
#include <velo/icons.h>

#define MAX_FILES 1024
#define SIDEBAR_WIDTH 200
#define DETAILS_HEIGHT 74
#define MAX_SYSTEM_DRIVES 8

static VeloDirEntry g_files[MAX_FILES];
static int g_file_count = 0;
static VeloDirEntry g_filtered[MAX_FILES];
static int g_filtered_count = 0;

static int g_selected = -1;
static int g_last_clicked = -1;
static char g_path[128] = "Computer";

static char g_history[16][128];
static int g_hist_pos = 0;

static char g_clipboard_path[128] = "";
static int g_clipboard_is_cut = 0;

static int g_ctx_open = 0;
static int g_ctx_x = 0;
static int g_ctx_y = 0;
static int g_ctx_is_item = 0;

#define CTX_ITEM_W 190
#define CTX_ITEM_H 140
#define CTX_BG_H   112

static int g_conflict_active = 0;
static char g_conflict_src[128] = "";
static char g_conflict_dst[256] = "";
static char g_conflict_name[32] = "";
static char g_conflict_suggested_name[32] = "";
static int g_conflict_is_cut = 0;

static int g_rename_active = 0;
static char g_rename_old_full[128] = "";
static char g_rename_input[32] = "";
static int g_rename_len = 0;
static int g_rename_cursor = 0;

static int g_new_item_active = 0;
static char g_new_item_input[32] = "";
static int g_new_item_len = 0;
static int g_new_item_cursor = 0;

static char g_search[32] = "";
static int g_search_len = 0;
static int g_search_cursor = 0;
static int g_search_focused = 0;

static int g_cur_w = 760;
static int g_cur_h = 490;

static VeloSysInfo g_sysinfo;
static VeloDriveInfo g_drives[MAX_SYSTEM_DRIVES];
static int g_drive_count = 0;

static void safe_str_copy(char *dst, const char *src, size_t max_len) {
    if (!dst || max_len == 0) return;
    size_t i = 0;
    if (src) {
        while (src[i] && i < max_len - 1) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

/* Ersetzt 8.3-Kurznamen in der Anzeige durch Klarnamen */
static void get_display_name(const char *raw_name, char *out, size_t max_len) {
    if (!raw_name || !out) return;
    if (!strcmp(raw_name, "DOCUME~1")) safe_str_copy(out, "Dokumente", max_len);
    else if (!strcmp(raw_name, "DOWNLA~1")) safe_str_copy(out, "Downloads", max_len);
    else if (!strcmp(raw_name, "PICTUR~1")) safe_str_copy(out, "Bilder", max_len);
    else if (!strcmp(raw_name, "PROGRA~1")) safe_str_copy(out, "Programme", max_len);
    else if (!strcmp(raw_name, "VELOOS"))   safe_str_copy(out, "VeloOS", max_len);
    else safe_str_copy(out, raw_name, max_len);
}

/* Wandelt Kurzpfade fuer die UI in vollstaendig lesbare Pfade um */
static void path_to_normalized(const char *src, char *dst, size_t max_len) {
    if (!src || !dst || max_len == 0) return;
    char temp[256];
    safe_str_copy(temp, src, sizeof(temp));

    char *p;
    while ((p = strstr(temp, "DOCUME~1")) != NULL) {
        char rest[256];
        safe_str_copy(rest, p + 8, sizeof(rest));
        sprintf(p, "Documents%s", rest);
    }
    while ((p = strstr(temp, "DOWNLA~1")) != NULL) {
        char rest[256];
        safe_str_copy(rest, p + 8, sizeof(rest));
        sprintf(p, "Downloads%s", rest);
    }
    while ((p = strstr(temp, "PICTUR~1")) != NULL) {
        char rest[256];
        safe_str_copy(rest, p + 8, sizeof(rest));
        sprintf(p, "Pictures%s", rest);
    }
    while ((p = strstr(temp, "PROGRA~1")) != NULL) {
        char rest[256];
        safe_str_copy(rest, p + 8, sizeof(rest));
        sprintf(p, "Program Files%s", rest);
    }
    safe_str_copy(dst, temp, max_len);
}

/* Wandelt Klarnamen fuer Dateisystemtreiber bei Bedarf in 8.3 um */
static void path_to_short(const char *src, char *dst, size_t max_len) {
    if (!src || !dst || max_len == 0) return;
    char temp[256];
    safe_str_copy(temp, src, sizeof(temp));

    char *p;
    while ((p = strstr(temp, "Documents")) != NULL) {
        char rest[256];
        safe_str_copy(rest, p + 9, sizeof(rest));
        sprintf(p, "DOCUME~1%s", rest);
    }
    while ((p = strstr(temp, "Downloads")) != NULL) {
        char rest[256];
        safe_str_copy(rest, p + 9, sizeof(rest));
        sprintf(p, "DOWNLA~1%s", rest);
    }
    while ((p = strstr(temp, "Pictures")) != NULL) {
        char rest[256];
        safe_str_copy(rest, p + 8, sizeof(rest));
        sprintf(p, "PICTUR~1%s", rest);
    }
    while ((p = strstr(temp, "Program Files")) != NULL) {
        char rest[256];
        safe_str_copy(rest, p + 13, sizeof(rest));
        sprintf(p, "PROGRA~1%s", rest);
    }
    safe_str_copy(dst, temp, max_len);
}

static int str_contains_nocase(const char *h, const char *n) {
    if (!n || !n[0]) return 1;
    if (!h) return 0;
    for (int i = 0; h[i] != '\0'; i++) {
        int m = 1;
        for (int k = 0; n[k] != '\0'; k++) {
            if (h[i+k] == '\0') { m = 0; break; }
            char c1 = (h[i+k] >= 'A' && h[i+k] <= 'Z') ? h[i+k] + 32 : h[i+k];
            char c2 = (n[k] >= 'A' && n[k] <= 'Z') ? n[k] + 32 : n[k];
            if (c1 != c2) { m = 0; break; }
        }
        if (m) return 1;
    }
    return 0;
}

static void apply_filter(void) {
    g_filtered_count = 0;
    for (int i = 0; i < g_file_count && g_filtered_count < MAX_FILES; i++) {
        char disp[64];
        get_display_name(g_files[i].name, disp, sizeof(disp));
        if (str_contains_nocase(g_files[i].name, g_search) || str_contains_nocase(disp, g_search)) {
            g_filtered[g_filtered_count++] = g_files[i];
        }
    }
    if (g_selected >= g_filtered_count) g_selected = -1;
}

static void reload_all_drives(void) {
    g_drive_count = 0;
    for (int i = 0; i < MAX_SYSTEM_DRIVES; i++) {
        memset(&g_drives[i], 0, sizeof(VeloDriveInfo));
        if (velo_get_drive_info(i, &g_drives[i]) == 0) {
            // Echte Mindestgroesse; behebt den unkalibrierten 64MB BIOS-Fallback
            if (g_drives[i].total_bytes <= 67108864ULL) {
                g_drives[i].total_bytes = 536870912ULL; // 512 MB Standard
                if (g_drives[i].free_bytes == 0) g_drives[i].free_bytes = 440401920ULL;
            }
            g_drive_count++;
        } else {
            break;
        }
    }
    if (g_drive_count == 0) {
        g_drive_count = 1;
        safe_str_copy(g_drives[0].label, "C:", sizeof(g_drives[0].label));
        safe_str_copy(g_drives[0].model, "Lokales Laufwerk", sizeof(g_drives[0].model));
        g_drives[0].total_bytes = 536870912ULL;
        g_drives[0].free_bytes = 440401920ULL;
    }
}

static void load_dir(const char *path, int record_history) {
    if (!path) return;

    char normalized[128];
    path_to_normalized(path, normalized, sizeof(normalized));
    safe_str_copy(g_path, normalized, sizeof(g_path));

    if (!strcmp(normalized, "Computer")) {
        reload_all_drives();
        g_file_count = 0;
        g_filtered_count = 0;
        g_selected = 0;
    } else {
        memset(g_files, 0, sizeof(g_files));
        g_file_count = velo_list_dir(g_path, g_files, MAX_FILES);

        // Falls das Dateisystem noch mit Kurznamen partitioniert ist:
        if (g_file_count <= 0) {
            char short_p[128];
            path_to_short(g_path, short_p, sizeof(short_p));
            g_file_count = velo_list_dir(short_p, g_files, MAX_FILES);
        }

        if (g_file_count < 0) g_file_count = 0;
        if (g_file_count > MAX_FILES) g_file_count = MAX_FILES;
        g_selected = -1;
        apply_filter();
    }

    if (record_history && g_hist_pos < 15) {
        safe_str_copy(g_history[g_hist_pos++], g_path, sizeof(g_history[0]));
    }
}

static int file_exists_in_current_dir(const char *name) {
    if (!name) return 0;
    for (int i = 0; i < g_file_count; i++) {
        if (!strcmp(g_files[i].name, name)) return 1;
    }
    return 0;
}

static void generate_unique_name(const char *name, char *out_name) {
    char base[32], ext[16];
    base[0] = '\0'; ext[0] = '\0';

    const char *dot = strrchr(name, '.');
    if (dot) {
        int blen = (int)(dot - name);
        if (blen > 18) blen = 18;
        safe_str_copy(base, name, (size_t)blen + 1);
        safe_str_copy(ext, dot, sizeof(ext));
    } else {
        safe_str_copy(base, name, 20);
        ext[0] = '\0';
    }

    for (int idx = 1; idx < 100; idx++) {
        sprintf(out_name, "%s(%d)%s", base, idx, ext);
        if (!file_exists_in_current_dir(out_name)) return;
    }
    sprintf(out_name, "%s_copy%s", base, ext);
}

static void get_selected_full_path(char *out_path) {
    out_path[0] = '\0';
    if (g_selected < 0 || g_selected >= g_filtered_count) return;
    const char *fname = g_filtered[g_selected].name;

    size_t l = strlen(g_path);
    if (l > 0 && (g_path[l - 1] == '/' || g_path[l - 1] == '\\')) {
        sprintf(out_path, "%s%s", g_path, fname);
    } else {
        sprintf(out_path, "%s/%s", g_path, fname);
    }
}

static void copy_selected(void) {
    if (g_selected < 0 || g_selected >= g_filtered_count) return;
    get_selected_full_path(g_clipboard_path);
    g_clipboard_is_cut = 0;
}

static void cut_selected(void) {
    if (g_selected < 0 || g_selected >= g_filtered_count) return;
    get_selected_full_path(g_clipboard_path);
    g_clipboard_is_cut = 1;
}

static void start_create_folder(void) {
    if (!strcmp(g_path, "Computer")) return;
    safe_str_copy(g_new_item_input, "NeuerOrdner", sizeof(g_new_item_input));
    g_new_item_len = (int)strlen(g_new_item_input);
    g_new_item_cursor = g_new_item_len;
    g_new_item_active = 1;
}

static void start_create_file(void) {
    if (!strcmp(g_path, "Computer")) return;
    safe_str_copy(g_new_item_input, "Neu.txt", sizeof(g_new_item_input));
    g_new_item_len = (int)strlen(g_new_item_input);
    g_new_item_cursor = g_new_item_len;
    g_new_item_active = 2;
}

static void confirm_create_item(void) {
    if (!g_new_item_active || g_new_item_len == 0) {
        g_new_item_active = 0;
        return;
    }

    char target_path[256];
    memset(target_path, 0, sizeof(target_path));
    
    size_t len = strlen(g_path);
    if (len > 0 && (g_path[len - 1] == '/' || g_path[len - 1] == '\\')) {
        snprintf(target_path, sizeof(target_path), "%s%s", g_path, g_new_item_input);
    } else {
        snprintf(target_path, sizeof(target_path), "%s/%s", g_path, g_new_item_input);
    }

    if (g_new_item_active == 1) {
        velo_mkdir(target_path);
    } else {
        velo_create_file(target_path);
    }

    g_new_item_active = 0;
    load_dir(g_path, 0);
}

static void cancel_create_item(void) { g_new_item_active = 0; }

static void start_rename_selected(void) {
    if (g_selected < 0 || g_selected >= g_filtered_count || !strcmp(g_path, "Computer")) return;
    get_selected_full_path(g_rename_old_full);
    safe_str_copy(g_rename_input, g_filtered[g_selected].name, sizeof(g_rename_input));
    g_rename_len = (int)strlen(g_rename_input);
    g_rename_cursor = g_rename_len;
    g_rename_active = 1;
}

static void confirm_rename(void) {
    if (!g_rename_active || g_rename_len == 0) { g_rename_active = 0; return; }
    if (g_selected < 0 || g_selected >= g_filtered_count) { g_rename_active = 0; return; }
    if (!strcmp(g_filtered[g_selected].name, g_rename_input)) { g_rename_active = 0; return; }

    if (file_exists_in_current_dir(g_rename_input)) {
        g_conflict_active = 1;
        safe_str_copy(g_conflict_src, g_rename_old_full, sizeof(g_conflict_src));
        size_t len = strlen(g_path);
        if (len > 0 && (g_path[len - 1] == '/' || g_path[len - 1] == '\\')) {
            snprintf(g_conflict_dst, sizeof(g_conflict_dst), "%s%s", g_path, g_rename_input);
        } else {
            snprintf(g_conflict_dst, sizeof(g_conflict_dst), "%s/%s", g_path, g_rename_input);
        }
        safe_str_copy(g_conflict_name, g_rename_input, sizeof(g_conflict_name));
        g_conflict_is_cut = 1;
        generate_unique_name(g_rename_input, g_conflict_suggested_name);
        g_rename_active = 0;
        return;
    }

    velo_rename_file(g_rename_old_full, g_rename_input);
    g_rename_active = 0;
    load_dir(g_path, 0);
}

static void cancel_rename(void) { g_rename_active = 0; }

static void paste_clipboard(void) {
    if (!g_clipboard_path[0] || !strcmp(g_path, "Computer")) return;

    const char *last_slash = strrchr(g_clipboard_path, '/');
    if (!last_slash) last_slash = strrchr(g_clipboard_path, '\\');
    const char *fname = last_slash ? last_slash + 1 : g_clipboard_path;

    char dst[256];
    size_t len = strlen(g_path);
    if (len > 0 && (g_path[len - 1] == '/' || g_path[len - 1] == '\\')) {
        sprintf(dst, "%s%s", g_path, fname);
    } else {
        sprintf(dst, "%s/%s", g_path, fname);
    }

    if (file_exists_in_current_dir(fname)) {
        g_conflict_active = 1;
        safe_str_copy(g_conflict_src, g_clipboard_path, sizeof(g_conflict_src));
        safe_str_copy(g_conflict_dst, dst, sizeof(g_conflict_dst));
        safe_str_copy(g_conflict_name, fname, sizeof(g_conflict_name));
        g_conflict_is_cut = g_clipboard_is_cut;
        generate_unique_name(fname, g_conflict_suggested_name);
        return;
    }

    if (g_clipboard_is_cut) {
        velo_move_file(g_clipboard_path, dst);
        g_clipboard_path[0] = '\0';
        g_clipboard_is_cut = 0;
    } else {
        velo_copy_file(g_clipboard_path, dst);
    }
    load_dir(g_path, 0);
}

static void conflict_resolve_replace(void) {
    if (strcmp(g_conflict_src, g_conflict_dst)) {
        if (g_conflict_is_cut) {
            velo_move_file(g_conflict_src, g_conflict_dst);
            g_clipboard_path[0] = '\0';
            g_clipboard_is_cut = 0;
        } else {
            velo_copy_file(g_conflict_src, g_conflict_dst);
        }
    }
    g_conflict_active = 0;
    load_dir(g_path, 0);
}

static void conflict_resolve_skip(void) { g_conflict_active = 0; }

static void conflict_resolve_keep_both(void) {
    char new_dst[256];
    size_t len = strlen(g_path);
    if (len > 0 && (g_path[len - 1] == '/' || g_path[len - 1] == '\\')) {
        sprintf(new_dst, "%s%s", g_path, g_conflict_suggested_name);
    } else {
        sprintf(new_dst, "%s/%s", g_path, g_conflict_suggested_name);
    }

    if (g_conflict_is_cut) {
        velo_move_file(g_conflict_src, new_dst);
        g_clipboard_path[0] = '\0';
        g_clipboard_is_cut = 0;
    } else {
        velo_copy_file(g_conflict_src, new_dst);
    }
    g_conflict_active = 0;
    load_dir(g_path, 0);
}

static void delete_selected(void) {
    if (g_selected < 0 || g_selected >= g_filtered_count || !strcmp(g_path, "Computer")) return;
    char full_p[256];
    get_selected_full_path(full_p);
    velo_delete_file(full_p);
    load_dir(g_path, 0);
}

static void format_date(UINT16 d, char *b) {
    if (!d) { sprintf(b, "12.03.2025"); return; }
    int day = d & 0x1F;
    int mon = (d >> 5) & 0x0F;
    int yr  = ((d >> 9) & 0x7F) + 1980;
    sprintf(b, "%d.%d.%d", day, mon, yr);
}

static void format_bytes_human(UINT64 bytes, char *b) {
    if (bytes >= 1024ULL * 1024 * 1024 * 1024) {
        UINT32 tb = (UINT32)(bytes / (1024ULL * 1024 * 1024 * 1024));
        sprintf(b, "%u TB", tb);
    } else if (bytes >= 1024ULL * 1024 * 1024) {
        UINT32 gb = (UINT32)(bytes / (1024ULL * 1024 * 1024));
        sprintf(b, "%u GB", gb);
    } else if (bytes >= 1024 * 1024) {
        UINT32 mb = (UINT32)(bytes / (1024 * 1024));
        sprintf(b, "%u MB", mb);
    } else if (bytes >= 1024) {
        sprintf(b, "%u KB", (unsigned int)(bytes / 1024));
    } else {
        sprintf(b, "%u Bytes", (unsigned int)bytes);
    }
}

static void format_sz(UINT32 s, int is_dir, char *b) {
    if (is_dir) sprintf(b, "Dateiordner");
    else format_bytes_human(s, b);
}

static int is_exe(const char *n) {
    if (!n) return 0;
    size_t l = strlen(n);
    return (l >= 4 && (!strcmp(n + l - 4, ".BIN") || !strcmp(n + l - 4, ".bin") ||
                       !strcmp(n + l - 4, ".EFI") || !strcmp(n + l - 4, ".efi")));
}

static int is_doc(const char *n) {
    if (!n) return 0;
    size_t l = strlen(n);
    return (l >= 4 && (!strcmp(n + l - 4, ".TXT") || !strcmp(n + l - 4, ".txt") ||
                       !strcmp(n + l - 4, ".DAT") || !strcmp(n + l - 4, ".dat") ||
                       !strcmp(n + l - 4, ".CFG") || !strcmp(n + l - 4, ".cfg")));
}

static void open_item(void) {
    if (!strcmp(g_path, "Computer")) {
        if (g_selected >= 0 && g_selected < g_drive_count) {
            char drive_path[8];
            sprintf(drive_path, "%s/", g_drives[g_selected].label);
            load_dir(drive_path, 1);
        }
    } else {
        if (g_selected < 0 || g_selected >= g_filtered_count) return;
        VeloDirEntry *cur = &g_filtered[g_selected];

        if (cur->is_dir) {
            char next[256];
            size_t l = strlen(g_path);
            if (l > 0 && (g_path[l - 1] == '/' || g_path[l - 1] == '\\')) {
                sprintf(next, "%s%s", g_path, cur->name);
            } else {
                sprintf(next, "%s/%s", g_path, cur->name);
            }
            load_dir(next, 1);
        } else if (is_exe(cur->name)) {
            char full_p[256];
            get_selected_full_path(full_p);
            velo_exec(full_p);
        } else if (is_doc(cur->name)) {
            char full_p[256];
            get_selected_full_path(full_p);
            velo_write_file("/VeloOS/System32/NOTEPAD_FILE.DAT", full_p, (UINT32)strlen(full_p));
            velo_write_file("/NOTEPAD_FILE.DAT", full_p, (UINT32)strlen(full_p));
            velo_exec("NOTEPAD.BIN");
        }
    }
}

static void render_explorer(velo_window_t win, int w, int h) {
    velo_window_clear(win);

    velo_ui_draw_nav_btn(win, 8, 6, "<", g_hist_pos > 1, 0);
    velo_ui_draw_nav_btn(win, 38, 6, ">", 0, 0);

    int addr_x = 72;
    int search_w = 160;
    int addr_w = w - addr_x - search_w - 14;
    if (addr_w < 160) addr_w = 160;

    char disp_path[256];
    if (!strcmp(g_path, "Computer")) {
        sprintf(disp_path, "Computer");
    } else {
        sprintf(disp_path, "Computer > %s", g_path);
    }

    velo_ui_draw_addressbar(win, addr_x, 6, addr_w, disp_path);

    int s_box_x = addr_x + addr_w + 6;
    velo_ui_draw_searchbox(win, s_box_x, 6, search_w, g_search, g_search_cursor, g_search_focused);

    velo_ui_draw_command_bar(win, 36, w);

    int sidebar_h = h - 64 - DETAILS_HEIGHT;
    if (sidebar_h > 0) {
        velo_window_draw_gradient(win, 0, 64, SIDEBAR_WIDTH, sidebar_h, 0x00FFFFFF, 0x00F8FAFC);
        velo_window_draw_rect_color(win, SIDEBAR_WIDTH - 1, 64, 1, sidebar_h, 0x00CBD5E1);

        velo_window_draw_text_colored(win, "Favoriten", 16, 74, 0x0064748B);

        velo_ui_draw_sidebar_item(win, 98, SIDEBAR_WIDTH, "    Computer", !strcmp(g_path, "Computer"));
        velo_window_draw_icon(win, VELO_ICON_PC, 10, 95, 16);

        velo_ui_draw_sidebar_item(win, 124, SIDEBAR_WIDTH, "    Dokumente", !strcmp(g_path, "C:/Users/Documents"));
        velo_window_draw_icon(win, VELO_ICON_FOLDER, 10, 121, 16);

        velo_ui_draw_sidebar_item(win, 150, SIDEBAR_WIDTH, "    Bilder", !strcmp(g_path, "C:/Users/Pictures"));
        velo_window_draw_icon(win, VELO_ICON_FOLDER, 10, 147, 16);

        velo_ui_draw_sidebar_item(win, 176, SIDEBAR_WIDTH, "    Downloads", !strcmp(g_path, "C:/Users/Downloads"));
        velo_window_draw_icon(win, VELO_ICON_FOLDER, 10, 173, 16);

        velo_ui_draw_sidebar_item(win, 202, SIDEBAR_WIDTH, "    Programme", !strcmp(g_path, "C:/Program Files"));
        velo_window_draw_icon(win, VELO_ICON_FOLDER, 10, 199, 16);

        velo_ui_draw_sidebar_item(win, 228, SIDEBAR_WIDTH, "    VeloOS", !strcmp(g_path, "C:/VeloOS"));
        velo_window_draw_icon(win, VELO_ICON_FOLDER, 10, 225, 16);
    }

    int main_x = SIDEBAR_WIDTH;
    int main_w = w - SIDEBAR_WIDTH;

    if (!strcmp(g_path, "Computer")) {
        int disk_start_x = main_x + 20;
        int disk_start_y = 80;

        char cat_header[64];
        sprintf(cat_header, "Festplattenlaufwerke (%d)", g_drive_count);
        velo_window_draw_text_colored(win, cat_header, disk_start_x, disk_start_y, 0x000369A1);
        if (main_w > 220) {
            velo_window_draw_rect_color(win, disk_start_x + 180, disk_start_y + 8, main_w - 220, 1, 0x00BAE6FD);
        }

        int card_y = disk_start_y + 24;
        for (int d = 0; d < g_drive_count; d++) {
            if (card_y + 64 > h - DETAILS_HEIGHT) break;

            if (g_selected == d) {
                velo_window_draw_gradient(win, disk_start_x, card_y, 300, 64, 0x00EBF4FB, 0x00CCE6FE);
                velo_window_draw_rect_color(win, disk_start_x, card_y, 300, 1, 0x0078B4E6);
                velo_window_draw_rect_color(win, disk_start_x, card_y + 63, 300, 1, 0x0078B4E6);
                velo_window_draw_rect_color(win, disk_start_x, card_y, 1, 64, 0x0078B4E6);
                velo_window_draw_rect_color(win, disk_start_x + 299, card_y, 1, 64, 0x0078B4E6);
            } else {
                velo_window_draw_rect_color(win, disk_start_x, card_y, 300, 64, 0x00F8FAFC);
            }

            velo_window_draw_icon(win, VELO_ICON_PC, disk_start_x + 12, card_y + 16, 32);

            char disk_title[64];
            sprintf(disk_title, "Datentraeger (%s)", g_drives[d].label);
            velo_window_draw_text_colored(win, disk_title, disk_start_x + 56, card_y + 8, 0x000F172A);

            char free_str[32], total_str[32], stat_str[80];
            format_bytes_human(g_drives[d].free_bytes, free_str);
            format_bytes_human(g_drives[d].total_bytes, total_str);
            sprintf(stat_str, "%s frei von %s", free_str, total_str);

            int used_pct = 0;
            if (g_drives[d].total_bytes > 0 && g_drives[d].total_bytes >= g_drives[d].free_bytes) {
                used_pct = (int)(((g_drives[d].total_bytes - g_drives[d].free_bytes) * 100ULL) / g_drives[d].total_bytes);
            }

            velo_ui_draw_storage_bar(win, disk_start_x + 56, card_y + 28, 220, used_pct);
            velo_window_draw_text_colored(win, stat_str, disk_start_x + 56, card_y + 46, 0x0064748B);

            card_y += 72;
        }

    } else {
        int tile_w = 230;
        int tile_h = 50;
        int gap_x = 12;
        int gap_y = 10;

        int avail_w = main_w - 30;
        int num_cols = (avail_w > 0) ? (avail_w / (tile_w + gap_x)) : 1;
        if (num_cols < 1) num_cols = 1;

        int start_x = main_x + 16;
        int start_y = 76;

        for (int i = 0; i < g_filtered_count; i++) {
            int col = i % num_cols;
            int row = i / num_cols;
            int tx = start_x + col * (tile_w + gap_x);
            int ty = start_y + row * (tile_h + gap_y);

            if (ty + tile_h > h - DETAILS_HEIGHT) break;

            if (i == g_selected) {
                velo_window_draw_gradient(win, tx, ty, tile_w, tile_h, 0x00EBF4FB, 0x00CCE6FE);
                velo_window_draw_rect_color(win, tx, ty, tile_w, 1, 0x0078B4E6);
                velo_window_draw_rect_color(win, tx, ty + tile_h - 1, tile_w, 1, 0x0078B4E6);
                velo_window_draw_rect_color(win, tx, ty, 1, tile_h, 0x0078B4E6);
                velo_window_draw_rect_color(win, tx + tile_w - 1, ty, 1, tile_h, 0x0078B4E6);
            }

            if (g_filtered[i].is_dir) {
                velo_window_draw_icon(win, VELO_ICON_FOLDER, tx + 8, ty + 9, 32);
            } else if (is_exe(g_filtered[i].name)) {
                velo_window_draw_icon(win, VELO_ICON_APP, tx + 8, ty + 9, 32);
            } else if (is_doc(g_filtered[i].name)) {
                velo_window_draw_icon(win, VELO_ICON_DOC, tx + 8, ty + 9, 32);
            } else {
                velo_window_draw_icon(win, VELO_ICON_UNKNOWN, tx + 8, ty + 9, 32);
            }

            char display_name[32];
            get_display_name(g_filtered[i].name, display_name, sizeof(display_name));

            char short_n[22];
            safe_str_copy(short_n, display_name, 21);
            velo_window_draw_text_colored(win, short_n, tx + 48, ty + 9, 0x000F172A);

            char sub_info[32];
            if (g_filtered[i].is_dir) {
                sprintf(sub_info, "Dateiordner");
            } else {
                char sz_str[16];
                format_bytes_human(g_filtered[i].size, sz_str);
                sprintf(sub_info, "%s", sz_str);
            }
            velo_window_draw_text_colored(win, sub_info, tx + 48, ty + 28, 0x0064748B);
        }

        if (!g_filtered_count) {
            velo_window_draw_text_colored(win, "Dieser Ordner ist leer.", main_x + 30, 100, 0x0064748B);
        }
    }

    int det_y = h - DETAILS_HEIGHT;
    if (det_y > 0) {
        velo_window_draw_gradient(win, 0, det_y, w, DETAILS_HEIGHT, 0x00E0F2FE, 0x00BAE6FD);
        velo_window_draw_rect_color(win, 0, det_y, w, 1, 0x007DD3FC);

        if (g_selected >= 0 && g_selected < g_filtered_count && strcmp(g_path, "Computer")) {
            VeloDirEntry *cur = &g_filtered[g_selected];
            char sz[32], d_str[16], meta1[96], meta2[96];
            format_sz(cur->size, cur->is_dir, sz);
            format_date(cur->date, d_str);

            char clean_disp[32];
            get_display_name(cur->name, clean_disp, sizeof(clean_disp));

            sprintf(meta1, "%s  |  Geaendert: %s", cur->is_dir ? "Dateiordner" : "Datei", d_str);
            sprintf(meta2, "Groesse: %s  |  Status: Bereit", sz);

            if (cur->is_dir) velo_window_draw_icon(win, VELO_ICON_FOLDER, 16, det_y + 16, 32);
            else if (is_exe(cur->name)) velo_window_draw_icon(win, VELO_ICON_APP, 16, det_y + 16, 32);
            else if (is_doc(cur->name)) velo_window_draw_icon(win, VELO_ICON_DOC, 16, det_y + 16, 32);
            else velo_window_draw_icon(win, VELO_ICON_UNKNOWN, 16, det_y + 16, 32);

            velo_window_draw_text_colored(win, clean_disp, 64, det_y + 10, 0x000F172A);
            velo_window_draw_text_colored(win, meta1, 64, det_y + 30, 0x00475569);
            velo_window_draw_text_colored(win, meta2, 64, det_y + 50, 0x00475569);

            velo_ui_draw_button(win, w - 280, det_y + 22, 80, 28, "Umbenennen", 0x0038BDF8, 0x000284C7, 0x00BAE6FD, 0x00FFFFFF);
            velo_ui_draw_button(win, w - 190, det_y + 22, 80, 28, "Loeschen", 0x00EF4444, 0x00DC2626, 0x00FCA5A5, 0x00FFFFFF);
            
            if (cur->is_dir || is_doc(cur->name)) {
                velo_ui_draw_button(win, w - 100, det_y + 22, 80, 28, "> Oeffnen", 0x0038BDF8, 0x000284C7, 0x00BAE6FD, 0x00FFFFFF);
            } else if (is_exe(cur->name)) {
                velo_ui_draw_button(win, w - 100, det_y + 22, 80, 28, "> Starten", 0x0016A34A, 0x0015803D, 0x0086EFAC, 0x00FFFFFF);
            }

        } else {
            velo_window_draw_icon(win, VELO_ICON_PC, 16, det_y + 16, 32);
            char pc_line[96], cpu_line[96], ram_line[64];
            sprintf(pc_line, "Computer  Arbeitsgruppe: WORKGROUP");
            sprintf(cpu_line, "Prozessor: %s", g_sysinfo.cpu_brand[0] ? g_sysinfo.cpu_brand : "x86_64 Prozessor");

            if (g_sysinfo.total_ram_mb >= 1000) {
                sprintf(ram_line, "RAM: %u GB", (unsigned int)(g_sysinfo.total_ram_mb / 1024));
            } else {
                sprintf(ram_line, "RAM: %u MB", (unsigned int)g_sysinfo.total_ram_mb);
            }

            velo_window_draw_text_colored(win, pc_line, 64, det_y + 10, 0x000F172A);
            velo_window_draw_text_colored(win, cpu_line, 64, det_y + 30, 0x00475569);
            velo_window_draw_text_colored(win, ram_line, 64, det_y + 50, 0x00475569);
        }
    }

    // Dialoge
    if (g_new_item_active) {
        int dlg_w = 420, dlg_h = 170;
        int dlg_x = (w - dlg_w) / 2, dlg_y = (h - dlg_h) / 2;
        velo_ui_draw_modal_dialog(win, dlg_x, dlg_y, dlg_w, dlg_h, 
            (g_new_item_active == 1) ? "Neuen Ordner erstellen" : "Neue Datei erstellen",
            (g_new_item_active == 1) ? "Ordnername eingeben:" : "Dateiname eingeben:",
            g_new_item_input, g_new_item_cursor, "Erstellen", "Abbrechen");
    }

    if (g_rename_active) {
        int dlg_w = 420, dlg_h = 170;
        int dlg_x = (w - dlg_w) / 2, dlg_y = (h - dlg_h) / 2;
        velo_ui_draw_modal_dialog(win, dlg_x, dlg_y, dlg_w, dlg_h,
            "Element umbenennen", "Neuen Namen eingeben:",
            g_rename_input, g_rename_cursor, "OK", "Abbrechen");
    }

    if (g_conflict_active) {
        int dlg_w = 480, dlg_h = 280;
        int dlg_x = (w - dlg_w) / 2, dlg_y = (h - dlg_h) / 2;
        velo_window_draw_rect_color(win, dlg_x - 3, dlg_y - 3, dlg_w + 6, dlg_h + 6, 0x0064748B);
        velo_window_draw_gradient(win, dlg_x, dlg_y, dlg_w, dlg_h, 0x00FFFFFF, 0x00F8FAFC);
        velo_window_draw_rect_color(win, dlg_x, dlg_y, dlg_w, 1, 0x000284C7);
        velo_window_draw_rect_color(win, dlg_x, dlg_y + dlg_h - 1, dlg_w, 1, 0x0094A3B8);
        velo_window_draw_gradient(win, dlg_x, dlg_y, dlg_w, 32, 0x001B4D68, 0x000A2434);
        velo_window_draw_text_colored(win, "Dateikonflikt - Datei existiert bereits", dlg_x + 14, dlg_y + 8, 0x00FFFFFF);

        char prompt_str[96];
        sprintf(prompt_str, "Ein Element namens '%s' existiert hier bereits.", g_conflict_name);
        velo_window_draw_text_colored(win, prompt_str, dlg_x + 20, dlg_y + 44, 0x000F172A);

        int op1_y = dlg_y + 70;
        velo_window_draw_gradient(win, dlg_x + 20, op1_y, dlg_w - 40, 48, 0x00F0F9FF, 0x00E0F2FE);
        velo_window_draw_rect_color(win, dlg_x + 20, op1_y, dlg_w - 40, 1, 0x007DD3FC);
        velo_window_draw_text_colored(win, "-> Ersetzen", dlg_x + 32, op1_y + 8, 0x000284C7);
        velo_window_draw_text_colored(win, "Die bestehende Datei im Zielordner ueberschreiben.", dlg_x + 32, op1_y + 26, 0x0064748B);

        int op2_y = dlg_y + 126;
        velo_window_draw_gradient(win, dlg_x + 20, op2_y, dlg_w - 40, 48, 0x00F8FAFC, 0x00F1F5F9);
        velo_window_draw_rect_color(win, dlg_x + 20, op2_y, dlg_w - 40, 1, 0x00CBD5E1);
        velo_window_draw_text_colored(win, "|| Nicht kopieren (Ueberspringen)", dlg_x + 32, op2_y + 8, 0x00334155);

        int op3_y = dlg_y + 182;
        velo_window_draw_gradient(win, dlg_x + 20, op3_y, dlg_w - 40, 48, 0x00F0FDF4, 0x00DCFCE7);
        velo_window_draw_rect_color(win, dlg_x + 20, op3_y, dlg_w - 40, 1, 0x0086EFAC);
        velo_window_draw_text_colored(win, "+  Beide Dateien behalten", dlg_x + 32, op3_y + 8, 0x0016A34A);

        int c_btn_x = dlg_x + dlg_w - 90;
        int c_btn_y = dlg_y + dlg_h - 38;
        velo_ui_draw_button(win, c_btn_x, c_btn_y, 70, 26, "Abbrechen", 0x00F1F5F9, 0x00E2E8F0, 0x00CBD5E1, 0x000F172A);
    }

    // Kontextmenü
    if (g_ctx_open) {
        int cx = g_ctx_x;
        int cy = g_ctx_y;
        int ch = g_ctx_is_item ? CTX_ITEM_H : CTX_BG_H;

        if (cx + CTX_ITEM_W > w - 8) cx = w - CTX_ITEM_W - 8;
        if (cy + ch > h - DETAILS_HEIGHT) cy = h - DETAILS_HEIGHT - ch - 4;
        if (cx < 4) cx = 4;
        if (cy < 4) cy = 4;

        velo_window_draw_rect_color(win, cx + 3, cy + 3, CTX_ITEM_W, ch, 0x0064748B);
        velo_window_draw_gradient(win, cx, cy, CTX_ITEM_W, ch, 0x00FFFFFF, 0x00F8FAFC);
        velo_window_draw_rect_color(win, cx, cy, CTX_ITEM_W, 1, 0x007BA3B8);
        velo_window_draw_rect_color(win, cx, cy + ch - 1, CTX_ITEM_W, 1, 0x007BA3B8);
        velo_window_draw_rect_color(win, cx, cy, 1, ch, 0x007BA3B8);
        velo_window_draw_rect_color(win, cx + CTX_ITEM_W - 1, cy, 1, ch, 0x007BA3B8);

        if (g_ctx_is_item) {
            velo_window_draw_text_colored(win, "> Oeffnen", cx + 12, cy + 8, 0x000F172A);
            velo_window_draw_rect_color(win, cx + 8, cy + 32, CTX_ITEM_W - 16, 1, 0x00E2E8F0);
            velo_window_draw_text_colored(win, "Umbenennen", cx + 12, cy + 38, 0x000F172A);
            velo_window_draw_text_colored(win, "Kopieren", cx + 12, cy + 64, 0x000F172A);
            velo_window_draw_text_colored(win, "Ausschneiden", cx + 12, cy + 88, 0x000F172A);
            velo_window_draw_rect_color(win, cx + 8, cy + 110, CTX_ITEM_W - 16, 1, 0x00E2E8F0);
            velo_window_draw_text_colored(win, "Loeschen", cx + 12, cy + 116, 0x00DC2626);
        } else {
            velo_window_draw_text_colored(win, "+ Neuer Ordner", cx + 12, cy + 8, 0x0016A34A);
            velo_window_draw_text_colored(win, "+ Neues Dokument", cx + 12, cy + 34, 0x0016A34A);
            velo_window_draw_rect_color(win, cx + 8, cy + 56, CTX_ITEM_W - 16, 1, 0x00E2E8F0);
            velo_window_draw_text_colored(win, "Einfuegen", cx + 12, cy + 62, g_clipboard_path[0] ? 0x000F172A : 0x0094A3B8);
            velo_window_draw_rect_color(win, cx + 8, cy + 84, CTX_ITEM_W - 16, 1, 0x00E2E8F0);
            velo_window_draw_text_colored(win, "~ Aktualisieren", cx + 12, cy + 90, 0x000284C7);
        }
    }

    velo_window_redraw();
}

static void nav_up(void) {
    if (!strcmp(g_path, "Computer")) return;
    
    if (strlen(g_path) <= 3 && g_path[1] == ':') {
        load_dir("Computer", 1);
        return;
    }

    char *s = strrchr(g_path, '/');
    if (!s) s = strrchr(g_path, '\\');

    if (s && s != g_path) {
        if (s - g_path == 2 && g_path[1] == ':') {
            *(s + 1) = '\0';
        } else {
            *s = '\0';
        }
        load_dir(g_path, 1);
    } else {
        load_dir("Computer", 1);
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    memset(&g_sysinfo, 0, sizeof(g_sysinfo));
    velo_get_sysinfo(&g_sysinfo);
    reload_all_drives();

    char start_path[128];
    memset(start_path, 0, sizeof(start_path));
    int bytes = velo_read_file("/VeloOS/System32/STARTDIR.DAT", start_path, sizeof(start_path) - 1);
    if (bytes <= 0) bytes = velo_read_file("/STARTDIR.DAT", start_path, sizeof(start_path) - 1);
    
    if (bytes > 0 && start_path[0]) {
        start_path[bytes] = '\0';
        velo_delete_file("/VeloOS/System32/STARTDIR.DAT");
        velo_delete_file("/STARTDIR.DAT");
        load_dir(start_path, 1);
    } else {
        load_dir("Computer", 1);
    }

    velo_window_t win = velo_window_create("Velo Explorer", g_cur_w, g_cur_h);
    if (win < 0) return 0;

    g_cur_w = velo_window_get_width(win);
    g_cur_h = velo_window_get_height(win);

    render_explorer(win, g_cur_w, g_cur_h);

    velo_event_t ev;
    while (1) {
        int st = velo_poll_event(win, &ev);
        if (st == -1) break;

        if (st == 0) {
            velo_thread_sleep(1);
            continue;
        }

        if (st == 1 && ev.type == VELO_EV_RESIZE) {
            g_cur_w = ev.x;
            g_cur_h = ev.y;
            render_explorer(win, g_cur_w, g_cur_h);
            continue;
        }

        if (st == 1 && ev.type == VELO_EV_RCLICK) {
            if (ev.y >= 64 && ev.y <= g_cur_h - DETAILS_HEIGHT && ev.x >= SIDEBAR_WIDTH) {
                int tile_w = 230, tile_h = 50, gap_x = 12, gap_y = 10;
                int avail_w = (g_cur_w - SIDEBAR_WIDTH) - 30;
                int num_cols = (avail_w > 0) ? (avail_w / (tile_w + gap_x)) : 1;
                if (num_cols < 1) num_cols = 1;

                int start_x = SIDEBAR_WIDTH + 16;
                int start_y = 76;
                int clicked_tile = -1;

                for (int i = 0; i < g_filtered_count; i++) {
                    int col = i % num_cols;
                    int row = i / num_cols;
                    int tx = start_x + col * (tile_w + gap_x);
                    int ty = start_y + row * (tile_h + gap_y);
                    if (velo_ui_in_rect(ev.x, ev.y, tx, ty, tile_w, tile_h)) {
                        clicked_tile = i;
                        break;
                    }
                }

                g_ctx_x = ev.x;
                g_ctx_y = ev.y;
                g_ctx_open = 1;

                if (clicked_tile >= 0) {
                    g_selected = clicked_tile;
                    g_ctx_is_item = 1;
                } else {
                    g_ctx_is_item = 0;
                }
                render_explorer(win, g_cur_w, g_cur_h);
                continue;
            }
        }

        if (st == 1 && ev.type == VELO_EV_CLICK) {
            if (g_ctx_open) {
                int cx = g_ctx_x;
                int cy = g_ctx_y;
                int ch = g_ctx_is_item ? CTX_ITEM_H : CTX_BG_H;
                if (cx + CTX_ITEM_W > g_cur_w - 8) cx = g_cur_w - CTX_ITEM_W - 8;
                if (cy + ch > g_cur_h - DETAILS_HEIGHT) cy = g_cur_h - DETAILS_HEIGHT - ch - 4;
                if (cx < 4) cx = 4;
                if (cy < 4) cy = 4;

                if (velo_ui_in_rect(ev.x, ev.y, cx, cy, CTX_ITEM_W, ch)) {
                    int local_y = ev.y - cy;
                    g_ctx_open = 0;

                    if (g_ctx_is_item) {
                        if (local_y >= 4 && local_y <= 28) open_item();
                        else if (local_y >= 34 && local_y <= 58) start_rename_selected();
                        else if (local_y >= 60 && local_y <= 84) copy_selected();
                        else if (local_y >= 86 && local_y <= 108) cut_selected();
                        else if (local_y >= 112 && local_y <= 136) delete_selected();
                    } else {
                        if (local_y >= 4 && local_y <= 30) start_create_folder();
                        else if (local_y >= 32 && local_y <= 54) start_create_file();
                        else if (local_y >= 58 && local_y <= 82) paste_clipboard();
                        else if (local_y >= 86 && local_y <= 110) load_dir(g_path, 0);
                    }
                    render_explorer(win, g_cur_w, g_cur_h);
                    continue;
                } else {
                    g_ctx_open = 0;
                    render_explorer(win, g_cur_w, g_cur_h);
                }
            }

            if (g_new_item_active) {
                int dlg_w = 420, dlg_h = 170;
                int dlg_x = (g_cur_w - dlg_w) / 2, dlg_y = (g_cur_h - dlg_h) / 2;
                int ok_x = dlg_x + dlg_w - 170, ok_y = dlg_y + dlg_h - 40;
                int can_x = dlg_x + dlg_w - 90;

                if (velo_ui_in_rect(ev.x, ev.y, ok_x, ok_y, 70, 26)) confirm_create_item();
                else if (velo_ui_in_rect(ev.x, ev.y, can_x, ok_y, 70, 26)) cancel_create_item();
                render_explorer(win, g_cur_w, g_cur_h);
                continue;
            }

            if (g_rename_active) {
                int dlg_w = 420, dlg_h = 170;
                int dlg_x = (g_cur_w - dlg_w) / 2, dlg_y = (g_cur_h - dlg_h) / 2;
                int ok_x = dlg_x + dlg_w - 170, ok_y = dlg_y + dlg_h - 40;
                int can_x = dlg_x + dlg_w - 90;

                if (velo_ui_in_rect(ev.x, ev.y, ok_x, ok_y, 70, 26)) confirm_rename();
                else if (velo_ui_in_rect(ev.x, ev.y, can_x, ok_y, 70, 26)) cancel_rename();
                render_explorer(win, g_cur_w, g_cur_h);
                continue;
            }

            if (g_conflict_active) {
                int dlg_w = 480, dlg_h = 280;
                int dlg_x = (g_cur_w - dlg_w) / 2, dlg_y = (g_cur_h - dlg_h) / 2;

                if (velo_ui_in_rect(ev.x, ev.y, dlg_x + 20, dlg_y + 70, dlg_w - 40, 48)) conflict_resolve_replace();
                else if (velo_ui_in_rect(ev.x, ev.y, dlg_x + 20, dlg_y + 126, dlg_w - 40, 48)) conflict_resolve_skip();
                else if (velo_ui_in_rect(ev.x, ev.y, dlg_x + 20, dlg_y + 182, dlg_w - 40, 48)) conflict_resolve_keep_both();
                else if (velo_ui_in_rect(ev.x, ev.y, dlg_x + dlg_w - 90, dlg_y + dlg_h - 38, 70, 26)) conflict_resolve_skip();
                render_explorer(win, g_cur_w, g_cur_h);
                continue;
            }

            // Zurück-Button
            if (velo_ui_in_rect(ev.x, ev.y, 8, 6, 26, 26)) {
                if (g_hist_pos > 1) {
                    g_hist_pos -= 2;
                    load_dir(g_history[g_hist_pos], 1);
                } else {
                    nav_up();
                }
                render_explorer(win, g_cur_w, g_cur_h);
                continue;
            }

            // Command Bar
            if (velo_ui_in_rect(ev.x, ev.y, 10, 36, 60, 28)) { start_create_folder(); render_explorer(win, g_cur_w, g_cur_h); continue; }
            else if (velo_ui_in_rect(ev.x, ev.y, 78, 36, 48, 28)) { start_create_file(); render_explorer(win, g_cur_w, g_cur_h); continue; }
            else if (velo_ui_in_rect(ev.x, ev.y, 134, 36, 38, 28)) { copy_selected(); render_explorer(win, g_cur_w, g_cur_h); continue; }
            else if (velo_ui_in_rect(ev.x, ev.y, 180, 36, 30, 28)) { cut_selected(); render_explorer(win, g_cur_w, g_cur_h); continue; }
            else if (velo_ui_in_rect(ev.x, ev.y, 218, 36, 46, 28)) { paste_clipboard(); render_explorer(win, g_cur_w, g_cur_h); continue; }
            else if (velo_ui_in_rect(ev.x, ev.y, 272, 36, 54, 28)) { start_rename_selected(); render_explorer(win, g_cur_w, g_cur_h); continue; }
            else if (velo_ui_in_rect(ev.x, ev.y, 334, 36, 54, 28)) { delete_selected(); render_explorer(win, g_cur_w, g_cur_h); continue; }

            // Suchfeld
            int s_box_x = 72 + (g_cur_w - 72 - 160 - 14) + 6;
            if (velo_ui_in_rect(ev.x, ev.y, s_box_x, 6, 160, 26)) {
                g_search_focused = 1;
                g_search_cursor = g_search_len;
                render_explorer(win, g_cur_w, g_cur_h);
                continue;
            } else if (g_search_focused) {
                g_search_focused = 0;
                render_explorer(win, g_cur_w, g_cur_h);
            }

            // Sidebar Links
            if (velo_ui_in_rect(ev.x, ev.y, 0, 90, SIDEBAR_WIDTH, 170)) {
                if (ev.y <= 112) load_dir("Computer", 1);
                else if (ev.y <= 138) load_dir("C:/Users/Documents", 1);
                else if (ev.y <= 164) load_dir("C:/Users/Pictures", 1);
                else if (ev.y <= 190) load_dir("C:/Users/Downloads", 1);
                else if (ev.y <= 216) load_dir("C:/Program Files", 1);
                else load_dir("C:/VeloOS", 1);
                render_explorer(win, g_cur_w, g_cur_h);
                continue;
            }

            // Hauptbereich
            if (!strcmp(g_path, "Computer")) {
                int clicked_drive = -1;
                int card_y = 104;
                for (int d = 0; d < g_drive_count; d++) {
                    if (velo_ui_in_rect(ev.x, ev.y, SIDEBAR_WIDTH + 20, card_y, 300, 64)) {
                        clicked_drive = d;
                        break;
                    }
                    card_y += 72;
                }

                if (clicked_drive >= 0) {
                    if (g_selected == clicked_drive && g_last_clicked == clicked_drive) {
                        open_item();
                        g_last_clicked = -1;
                    } else {
                        g_selected = clicked_drive;
                        g_last_clicked = clicked_drive;
                    }
                    render_explorer(win, g_cur_w, g_cur_h);
                }
            } else {
                int tile_w = 230, tile_h = 50, gap_x = 12, gap_y = 10;
                int avail_w = (g_cur_w - SIDEBAR_WIDTH) - 30;
                int num_cols = (avail_w > 0) ? (avail_w / (tile_w + gap_x)) : 1;
                if (num_cols < 1) num_cols = 1;

                int start_x = SIDEBAR_WIDTH + 16;
                int start_y = 76;
                int clicked_tile = -1;

                for (int i = 0; i < g_filtered_count; i++) {
                    int col = i % num_cols;
                    int row = i / num_cols;
                    int tx = start_x + col * (tile_w + gap_x);
                    int ty = start_y + row * (tile_h + gap_y);

                    if (velo_ui_in_rect(ev.x, ev.y, tx, ty, tile_w, tile_h)) {
                        clicked_tile = i;
                        break;
                    }
                }

                if (clicked_tile >= 0) {
                    if (g_selected == clicked_tile && g_last_clicked == clicked_tile) {
                        open_item();
                        g_last_clicked = -1;
                    } else {
                        g_selected = clicked_tile;
                        g_last_clicked = clicked_tile;
                    }
                    render_explorer(win, g_cur_w, g_cur_h);
                } else if (ev.y > g_cur_h - DETAILS_HEIGHT) {
                    if (velo_ui_in_rect(ev.x, ev.y, g_cur_w - 280, g_cur_h - DETAILS_HEIGHT + 22, 80, 28)) {
                        start_rename_selected();
                    } else if (velo_ui_in_rect(ev.x, ev.y, g_cur_w - 190, g_cur_h - DETAILS_HEIGHT + 22, 80, 28)) {
                        delete_selected();
                    } else if (velo_ui_in_rect(ev.x, ev.y, g_cur_w - 100, g_cur_h - DETAILS_HEIGHT + 22, 80, 28)) {
                        open_item();
                    }
                    render_explorer(win, g_cur_w, g_cur_h);
                }
            }
        }

        if (st == 1 && ev.type == VELO_EV_KEY) {
            if (ev.key == KEY_CTRL_C) { copy_selected(); render_explorer(win, g_cur_w, g_cur_h); continue; }
            if (ev.key == KEY_CTRL_X) { cut_selected(); render_explorer(win, g_cur_w, g_cur_h); continue; }
            if (ev.key == KEY_CTRL_V) { paste_clipboard(); render_explorer(win, g_cur_w, g_cur_h); continue; }
            if (ev.key == KEY_CTRL_A) { g_selected = 0; render_explorer(win, g_cur_w, g_cur_h); continue; }

            if (ev.key == (char)0x1B && g_ctx_open) {
                g_ctx_open = 0;
                render_explorer(win, g_cur_w, g_cur_h);
                continue;
            }

            if (g_new_item_active) {
                if (ev.key == '\n') confirm_create_item();
                else if (ev.key == (char)0x1B) cancel_create_item();
                else if (ev.key == KEY_LEFT || ev.key == (char)0x84) { if (g_new_item_cursor > 0) g_new_item_cursor--; }
                else if (ev.key == KEY_RIGHT || ev.key == (char)0x85) { if (g_new_item_cursor < g_new_item_len) g_new_item_cursor++; }
                else if (ev.key == '\b') {
                    if (g_new_item_cursor > 0) {
                        for (int i = g_new_item_cursor - 1; i < g_new_item_len; i++) {
                            g_new_item_input[i] = g_new_item_input[i + 1];
                        }
                        g_new_item_len--; g_new_item_cursor--;
                    }
                } else if ((unsigned char)ev.key >= 32 && (unsigned char)ev.key < 127 && g_new_item_len < 26) {
                    for (int i = g_new_item_len; i >= g_new_item_cursor; i--) g_new_item_input[i + 1] = g_new_item_input[i];
                    g_new_item_input[g_new_item_cursor] = ev.key;
                    g_new_item_len++; g_new_item_cursor++;
                }
                render_explorer(win, g_cur_w, g_cur_h);
                continue;
            }

            if (g_rename_active) {
                if (ev.key == '\n') confirm_rename();
                else if (ev.key == (char)0x1B) cancel_rename();
                else if (ev.key == KEY_LEFT || ev.key == (char)0x84) { if (g_rename_cursor > 0) g_rename_cursor--; }
                else if (ev.key == KEY_RIGHT || ev.key == (char)0x85) { if (g_rename_cursor < g_rename_len) g_rename_cursor++; }
                else if (ev.key == '\b') {
                    if (g_rename_cursor > 0) {
                        for (int i = g_rename_cursor - 1; i < g_rename_len; i++) {
                            g_rename_input[i] = g_rename_input[i + 1];
                        }
                        g_rename_len--; g_rename_cursor--;
                    }
                } else if ((unsigned char)ev.key >= 32 && (unsigned char)ev.key < 127 && g_rename_len < 26) {
                    for (int i = g_rename_len; i >= g_rename_cursor; i--) g_rename_input[i + 1] = g_rename_input[i];
                    g_rename_input[g_rename_cursor] = ev.key;
                    g_rename_len++; g_rename_cursor++;
                }
                render_explorer(win, g_cur_w, g_cur_h);
                continue;
            }

            if ((unsigned char)ev.key == 0x88 || (unsigned char)ev.key == 0x7F) {
                if (!g_search_focused && g_selected >= 0 && g_selected < g_filtered_count) {
                    delete_selected();
                    render_explorer(win, g_cur_w, g_cur_h);
                    continue;
                }
            }

            if (ev.key == '\n') { open_item(); render_explorer(win, g_cur_w, g_cur_h); }
            else if (!g_search_focused && (ev.key == 'r' || ev.key == 'R')) { start_rename_selected(); render_explorer(win, g_cur_w, g_cur_h); }
            else if (!g_search_focused && (ev.key == 'c' || ev.key == 'C')) { copy_selected(); render_explorer(win, g_cur_w, g_cur_h); }
            else if (!g_search_focused && (ev.key == 'x' || ev.key == 'X')) { cut_selected(); render_explorer(win, g_cur_w, g_cur_h); }
            else if (!g_search_focused && (ev.key == 'v' || ev.key == 'V')) { paste_clipboard(); render_explorer(win, g_cur_w, g_cur_h); }
            else if (g_search_focused) {
                if (ev.key == '\b') {
                    if (g_search_cursor > 0) {
                        for (int i = g_search_cursor - 1; i < g_search_len; i++) g_search[i] = g_search[i + 1];
                        g_search_len--; g_search_cursor--;
                        apply_filter();
                        render_explorer(win, g_cur_w, g_cur_h);
                    }
                } else if ((unsigned char)ev.key >= 32 && (unsigned char)ev.key < 127 && g_search_len < 20) {
                    for (int i = g_search_len; i >= g_search_cursor; i--) g_search[i + 1] = g_search[i];
                    g_search[g_search_cursor] = ev.key;
                    g_search_len++; g_search_cursor++;
                    apply_filter();
                    render_explorer(win, g_cur_w, g_cur_h);
                }
            }
        }
    }

    return 0;
}
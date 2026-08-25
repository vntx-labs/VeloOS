#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <velo/window.h>

#define MAX_FILES 64
#define SIDEBAR_WIDTH 210
#define DETAILS_HEIGHT 74

static VeloDirEntry g_files[MAX_FILES];
static int g_file_count = 0;
static VeloDirEntry g_filtered[MAX_FILES];
static int g_filtered_count = 0;

static int g_selected = -1;
static char g_path[128] = "Computer";

static char g_history[16][128];
static int g_hist_pos = 0;

static char g_search[32] = "";
static int g_search_len = 0;
static int g_search_focused = 0;

static int g_cur_w = 840;
static int g_cur_h = 530;

static VeloSysInfo g_sysinfo;
static VeloDriveInfo g_drive;

static int str_contains_nocase(const char *h, const char *n) {
    if (!n || !n[0]) return 1;
    for (int i = 0; h[i]; i++) {
        int m = 1;
        for (int k = 0; n[k]; k++) {
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
    for (int i = 0; i < g_file_count; i++) {
        if (str_contains_nocase(g_files[i].name, g_search)) {
            g_filtered[g_filtered_count++] = g_files[i];
        }
    }
    if (g_selected >= g_filtered_count) g_selected = -1;
}

static void load_dir(const char *path, int record_history) {
    strcpy(g_path, path);
    if (!strcmp(path, "Computer")) {
        g_file_count = 0;
        g_filtered_count = 0;
        g_selected = 0;
    } else {
        memset(g_files, 0, sizeof(g_files));
        const char *fs_path = (!strcmp(path, "C:") || !strcmp(path, "C:/")) ? "/" : path;
        g_file_count = velo_list_dir(fs_path, g_files, MAX_FILES);
        if (g_file_count < 0) g_file_count = 0;
        g_selected = -1;
        apply_filter();
    }

    if (record_history && g_hist_pos < 15) {
        strcpy(g_history[g_hist_pos++], path);
    }
}

static void format_date(UINT16 d, char *b) {
    if (!d) { sprintf(b, "12.03.2025"); return; }
    sprintf(b, "%02d.%02d.%04d", d & 0x1F, (d >> 5) & 0x0F, ((d >> 9) & 0x7F) + 1980);
}

static void format_bytes_human(UINT64 bytes, char *b) {
    if (bytes >= 1024ULL * 1024 * 1024) {
        UINT32 gb = (UINT32)(bytes / (1024ULL * 1024 * 1024));
        UINT32 frac = (UINT32)((bytes % (1024ULL * 1024 * 1024)) / (1024 * 1024 * 100));
        sprintf(b, "%u.%u GB", gb, frac);
    } else if (bytes >= 1024 * 1024) {
        UINT32 mb = (UINT32)(bytes / (1024 * 1024));
        UINT32 frac = (UINT32)((bytes % (1024 * 1024)) / (1024 * 100));
        sprintf(b, "%u.%u MB", mb, frac);
    } else if (bytes >= 1024) {
        sprintf(b, "%u KB", (unsigned int)(bytes / 1024));
    } else {
        sprintf(b, "%u Bytes", (unsigned int)bytes);
    }
}

static void format_sz(UINT32 s, int is_dir, char *b) {
    if (is_dir) sprintf(b, "<Ordner>");
    else format_bytes_human(s, b);
}

static int is_exe(const char *n) {
    size_t l = strlen(n);
    return (l >= 4 && (!strcmp(n + l - 4, ".BIN") || !strcmp(n + l - 4, ".EFI")));
}

/* ====================================================
 * VISTA EXPLORER RENDERING
 * ==================================================== */
static void render_explorer(velo_window_t win, int w, int h) {
    velo_window_clear(win);

    // 1. OBERE NAVIGATIONSLEISTE
    velo_ui_nav_btn(win, 8, 6, "<", g_hist_pos > 1, 0);
    velo_ui_nav_btn(win, 38, 6, ">", 0, 0);

    int addr_x = 72;
    int search_w = 170;
    int addr_w = w - addr_x - search_w - 14;
    if (addr_w < 180) addr_w = 180;

    char disp_path[160];
    if (!strcmp(g_path, "Computer")) {
        sprintf(disp_path, "Computer");
    } else if (!strcmp(g_path, "C:") || !strcmp(g_path, "/")) {
        sprintf(disp_path, "Computer > Local Disk (C:)");
    } else {
        sprintf(disp_path, "Computer > Local Disk (C:)%s", g_path);
    }

    velo_ui_addressbar(win, addr_x, 6, addr_w, disp_path);
    velo_ui_searchbox(win, addr_x + addr_w + 6, 6, search_w, g_search, g_search_focused);

    // 2. VISTA AERO GLASS COMMAND BAR
    velo_ui_command_bar(win, 36, w);

    // 3. FAVORITE LINKS SIDEBAR (210px) MIT SAUBEREN SHORTCUTS
    int sidebar_h = h - 64 - DETAILS_HEIGHT;

    velo_window_draw_gradient(win, 0, 64, SIDEBAR_WIDTH, sidebar_h, 0x00FFFFFF, 0x00F8FAFC);
    velo_window_draw_rect_color(win, SIDEBAR_WIDTH - 1, 64, 1, sidebar_h, 0x00CBD5E1);

    velo_window_draw_text_colored(win, "Favorite Links", 16, 76, 0x0064748B);
    
    // Klare Shortcuts mit eindeutigen Zielen
    velo_ui_sidebar_item(win, 102, SIDEBAR_WIDTH, " [PC] Computer", !strcmp(g_path, "Computer"));
    velo_ui_sidebar_item(win, 126, SIDEBAR_WIDTH, " [=] Documents", !strcmp(g_path, "/Users/Documents"));
    velo_ui_sidebar_item(win, 150, SIDEBAR_WIDTH, " [#] Pictures", !strcmp(g_path, "/Users/Pictures"));
    velo_ui_sidebar_item(win, 174, SIDEBAR_WIDTH, " [o] Music", !strcmp(g_path, "/Users/Music"));
    velo_ui_sidebar_item(win, 198, SIDEBAR_WIDTH, " [v] Downloads", !strcmp(g_path, "/Users/Downloads"));
    velo_ui_sidebar_item(win, 222, SIDEBAR_WIDTH, " [*] VeloOS", !strcmp(g_path, "/VeloOS"));
    velo_ui_sidebar_item(win, 246, SIDEBAR_WIDTH, " [+] Public", !strcmp(g_path, "/Users/Public"));

    // Folders Ausklapp-Leiste
    int fold_y = 64 + sidebar_h - 28;
    velo_window_draw_gradient(win, 0, fold_y, SIDEBAR_WIDTH - 1, 28, 0x00F1F5F9, 0x00E2E8F0);
    velo_window_draw_rect_color(win, 0, fold_y, SIDEBAR_WIDTH - 1, 1, 0x00CBD5E1);
    velo_window_draw_text_colored(win, "Folders", 16, fold_y + 6, 0x000F172A);
    velo_window_draw_text_colored(win, "^", SIDEBAR_WIDTH - 24, fold_y + 6, 0x001D4ED8);

    // 4. HAUPTINHALTSBEREICH RECHTS
    int main_x = SIDEBAR_WIDTH;
    int main_w = w - SIDEBAR_WIDTH;

    velo_window_draw_gradient(win, main_x, 64, main_w, 24, 0x00FFFFFF, 0x00F1F5F9);
    velo_window_draw_rect_color(win, main_x, 87, main_w, 1, 0x00CBD5E1);

    velo_window_draw_text_colored(win, "Name", main_x + 14, 69, 0x00475569);
    velo_window_draw_rect_color(win, main_x + 110, 64, 1, 24, 0x00CBD5E1);

    velo_window_draw_gradient(win, main_x + 111, 64, 88, 23, 0x00E0F2FE, 0x00BAE6FD);
    velo_window_draw_rect_color(win, main_x + 111, 64, 88, 2, 0x000284C7);
    velo_window_draw_text_colored(win, "Type  v", main_x + 124, 69, 0x000284C7);
    velo_window_draw_rect_color(win, main_x + 199, 64, 1, 24, 0x00CBD5E1);

    velo_window_draw_text_colored(win, "Total Size", main_x + 212, 69, 0x00475569);
    velo_window_draw_rect_color(win, main_x + 310, 64, 1, 24, 0x00CBD5E1);

    velo_window_draw_text_colored(win, "Free Space", main_x + 322, 69, 0x00475569);

    if (!strcmp(g_path, "Computer")) {
        // ==========================================
        // VISTA COMPUTER LAUFWERKSANSICHT
        // ==========================================
        int cat1_y = 98;
        velo_window_draw_text_colored(win, "Hard Disk Drives (1)", main_x + 16, cat1_y, 0x000369A1);
        velo_window_draw_rect_color(win, main_x + 180, cat1_y + 8, main_w - 210, 1, 0x00BAE6FD);
        velo_window_draw_text_colored(win, "^", main_x + main_w - 20, cat1_y, 0x000369A1);

        int disk1_x = main_x + 16;
        int disk1_y = cat1_y + 26;

        if (g_selected == 0) {
            velo_window_draw_gradient(win, disk1_x - 4, disk1_y - 4, 290, 60, 0x00EBF4FB, 0x00D6ECFF);
            velo_window_draw_rect_color(win, disk1_x - 4, disk1_y - 4, 290, 1, 0x0078B4E6);
            velo_window_draw_rect_color(win, disk1_x - 4, disk1_y + 55, 290, 1, 0x0078B4E6);
            velo_window_draw_rect_color(win, disk1_x - 4, disk1_y - 4, 1, 60, 0x0078B4E6);
            velo_window_draw_rect_color(win, disk1_x + 285, disk1_y - 4, 1, 60, 0x0078B4E6);
        }

        char disk_title[64];
        sprintf(disk_title, "Local Disk (%s)", g_drive.label[0] ? g_drive.label : "C:");

        char free_str[32], total_str[32], stat_str[80];
        format_bytes_human(g_drive.free_bytes, free_str);
        format_bytes_human(g_drive.total_bytes, total_str);
        sprintf(stat_str, "%s free of %s", free_str, total_str);

        int used_pct = 0;
        if (g_drive.total_bytes > 0 && g_drive.total_bytes >= g_drive.free_bytes) {
            used_pct = (int)(((g_drive.total_bytes - g_drive.free_bytes) * 100ULL) / g_drive.total_bytes);
        }
        if (used_pct < 5) used_pct = 5;

        velo_window_draw_text_colored(win, "[HDD]", disk1_x + 4, disk1_y + 14, 0x000284C7);
        velo_window_draw_text_colored(win, disk_title, disk1_x + 60, disk1_y + 2, 0x000F172A);
        
        velo_ui_vista_storage_bar(win, disk1_x + 60, disk1_y + 20, 210, used_pct);
        velo_window_draw_text_colored(win, stat_str, disk1_x + 60, disk1_y + 38, 0x0064748B);

    } else {
        // ==========================================
        // ECHTE DATEI- & ORDNERANSICHT
        // ==========================================
        int row_y = 96;
        int max_rows = (sidebar_h - 36) / 26;

        for (int i = 0; i < g_filtered_count && i < max_rows; i++) {
            char d_str[16], s_str[16];
            format_date(g_filtered[i].date, d_str);
            format_sz(g_filtered[i].size, g_filtered[i].is_dir, s_str);

            const char *icon = g_filtered[i].is_dir ? "[DIR]" : (is_exe(g_filtered[i].name) ? "[APP]" : "[DOC]");
            const char *type = g_filtered[i].is_dir ? "File folder" : (is_exe(g_filtered[i].name) ? "Application" : "Document");

            if (i == g_selected) {
                velo_window_draw_gradient(win, main_x + 2, row_y - 3, main_w - 4, 24, 0x00EBF4FB, 0x00CCE6FE);
                velo_window_draw_rect_color(win, main_x + 2, row_y - 3, main_w - 4, 1, 0x0078B4E6);
                velo_window_draw_rect_color(win, main_x + 2, row_y + 20, main_w - 4, 1, 0x0078B4E6);
            }

            velo_window_draw_text_colored(win, icon, main_x + 8, row_y + 1, 0x000284C7);
            velo_window_draw_text_colored(win, g_filtered[i].name, main_x + 56, row_y + 1, 0x000F172A);
            velo_window_draw_text_colored(win, d_str, main_x + 220, row_y + 1, 0x0064748B);
            velo_window_draw_text_colored(win, type, main_x + 360, row_y + 1, 0x00334155);
            velo_window_draw_text_colored(win, s_str, main_x + 460, row_y + 1, 0x0064748B);
            row_y += 26;
        }

        if (!g_filtered_count) {
            velo_window_draw_text_colored(win, "This folder is empty.", main_x + 40, 120, 0x0064748B);
        }
    }

    // 5. VISTA DETAILS PANE AM UNTEREN RAND
    int det_y = h - DETAILS_HEIGHT;
    velo_window_draw_gradient(win, 0, det_y, w, DETAILS_HEIGHT, 0x00E0F2FE, 0x00BAE6FD);
    velo_window_draw_rect_color(win, 0, det_y, w, 1, 0x007DD3FC);

    if (g_selected >= 0 && g_selected < g_filtered_count && strcmp(g_path, "Computer")) {
        VeloDirEntry *cur = &g_filtered[g_selected];
        char sz[32], d_str[16], meta1[96], meta2[96];
        format_sz(cur->size, cur->is_dir, sz);
        format_date(cur->date, d_str);

        sprintf(meta1, "%s  |  Date modified: %s", cur->is_dir ? "File folder" : "File", d_str);
        sprintf(meta2, "Size: %s  |  Availability: Online", sz);

        const char *btn = cur->is_dir ? "> Open" : (is_exe(cur->name) ? "> Run" : "> View");
        const char *icn = cur->is_dir ? "[DIR]" : (is_exe(cur->name) ? "[APP]" : "[DOC]");

        velo_window_draw_text_colored(win, icn, 18, det_y + 16, 0x000284C7);
        velo_window_draw_text_colored(win, cur->name, 76, det_y + 10, 0x000F172A);
        velo_window_draw_text_colored(win, meta1, 76, det_y + 30, 0x00475569);
        velo_window_draw_text_colored(win, meta2, 76, det_y + 50, 0x00475569);

        int btn_w = 110;
        int btn_x = w - btn_w - 20;
        int btn_y = det_y + 22;
        velo_window_draw_gradient(win, btn_x, btn_y, btn_w, 28, 0x0038BDF8, 0x000284C7);
        velo_window_draw_rect_color(win, btn_x, btn_y, btn_w, 1, 0x00BAE6FD);
        velo_window_draw_rect_color(win, btn_x, btn_y + 27, btn_w, 1, 0x000369A1);
        velo_window_draw_text_colored(win, btn, btn_x + 18, btn_y + 6, 0x00FFFFFF);

    } else {
        char pc_line[96], cpu_line[96], ram_line[64];
        sprintf(pc_line, "%s  Workgroup: WORKGROUP", g_sysinfo.pc_name[0] ? g_sysinfo.pc_name : "VELO-PC");
        sprintf(cpu_line, "Processor: %s", g_sysinfo.cpu_brand[0] ? g_sysinfo.cpu_brand : "x86_64 Processor");

        if (g_sysinfo.total_ram_mb >= 1000) {
            sprintf(ram_line, "Memory: %u.%02u GB", (unsigned int)(g_sysinfo.total_ram_mb / 1024), (unsigned int)((g_sysinfo.total_ram_mb % 1024) * 100 / 1024));
        } else {
            sprintf(ram_line, "Memory: %u MB", (unsigned int)g_sysinfo.total_ram_mb);
        }

        velo_window_draw_text_colored(win, "[PC]", 18, det_y + 16, 0x000284C7);
        velo_window_draw_text_colored(win, pc_line, 76, det_y + 10, 0x000F172A);
        velo_window_draw_text_colored(win, cpu_line, 76, det_y + 30, 0x00475569);
        velo_window_draw_text_colored(win, ram_line, 76, det_y + 50, 0x00475569);

        velo_window_draw_text_colored(win, "~~~~~~~", w - 90, det_y + 46, 0x007DD3FC);
        velo_window_draw_text_colored(win, "~~~~~", w - 70, det_y + 34, 0x00BAE6FD);
    }

    velo_window_redraw();
}

static void open_item(void) {
    if (!strcmp(g_path, "Computer")) {
        if (g_selected == 0) {
            load_dir("C:", 1);
        }
    } else {
        if (g_selected < 0 || g_selected >= g_filtered_count) return;
        VeloDirEntry *cur = &g_filtered[g_selected];

        if (cur->is_dir) {
            char next[256];
            if (!strcmp(g_path, "C:") || !strcmp(g_path, "/")) {
                sprintf(next, "/%s", cur->name);
            } else {
                size_t l = strlen(g_path);
                if (l > 0 && g_path[l - 1] == '/') sprintf(next, "%s%s", g_path, cur->name);
                else sprintf(next, "%s/%s", g_path, cur->name);
            }
            load_dir(next, 1);
        } else if (is_exe(cur->name)) {
            velo_exec(cur->name);
        }
    }
}

static void nav_up(void) {
    if (!strcmp(g_path, "Computer")) return;
    if (!strcmp(g_path, "C:") || !strcmp(g_path, "/")) {
        load_dir("Computer", 1);
        return;
    }
    char *s = strrchr(g_path, '/');
    if (s && s != g_path) *s = '\0';
    else strcpy(g_path, "C:");
    load_dir(g_path, 1);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    memset(&g_sysinfo, 0, sizeof(g_sysinfo));
    memset(&g_drive, 0, sizeof(g_drive));
    velo_get_sysinfo(&g_sysinfo);
    velo_get_drive_info(0, &g_drive);

    load_dir("Computer", 1);

    velo_window_t win = velo_window_create("Velo Explorer", g_cur_w, g_cur_h);
    if (win < 0) return 1;

    g_cur_w = velo_window_get_width(win);
    g_cur_h = velo_window_get_height(win);

    render_explorer(win, g_cur_w, g_cur_h);

    velo_event_t ev;
    while (1) {
        int st = velo_poll_event(win, &ev);
        if (st == -1) break;

        // 1. RESIZE EVENT
        if (st == 1 && ev.type == VELO_EV_RESIZE) {
            g_cur_w = ev.x;
            g_cur_h = ev.y;
            render_explorer(win, g_cur_w, g_cur_h);
        }

        // 2. MOUSE CLICKS
        if (st == 1 && ev.type == VELO_EV_CLICK) {
            // Zurueck-Button
            if (velo_ui_in_rect(ev.x, ev.y, 8, 6, 26, 26)) {
                if (g_hist_pos > 1) {
                    g_hist_pos -= 2;
                    load_dir(g_history[g_hist_pos], 1);
                } else {
                    nav_up();
                }
                render_explorer(win, g_cur_w, g_cur_h);
            }

            // Suchfeld
            if (velo_ui_in_rect(ev.x, ev.y, g_cur_w - 180, 6, 170, 26)) {
                g_search_focused = 1;
                render_explorer(win, g_cur_w, g_cur_h);
            } else if (g_search_focused) {
                g_search_focused = 0;
                render_explorer(win, g_cur_w, g_cur_h);
            }

            // Favorite Links Sidebar (Exakte Shortcuts)
            if (velo_ui_in_rect(ev.x, ev.y, 0, 92, SIDEBAR_WIDTH, 180)) {
                if (ev.y <= 114) load_dir("Computer", 1);
                else if (ev.y <= 138) load_dir("/Users/Documents", 1);
                else if (ev.y <= 162) load_dir("/Users/Pictures", 1);
                else if (ev.y <= 186) load_dir("/Users/Music", 1);
                else if (ev.y <= 210) load_dir("/Users/Downloads", 1);
                else if (ev.y <= 234) load_dir("/VeloOS", 1);
                else load_dir("/Users/Public", 1);
                render_explorer(win, g_cur_w, g_cur_h);
            }

            // Hauptbereich Klicks
            if (!strcmp(g_path, "Computer")) {
                if (velo_ui_in_rect(ev.x, ev.y, SIDEBAR_WIDTH + 16, 120, 290, 60)) {
                    g_selected = 0;
                    open_item();
                    render_explorer(win, g_cur_w, g_cur_h);
                }
            } else {
                if (velo_ui_in_rect(ev.x, ev.y, SIDEBAR_WIDTH, 96, g_cur_w - SIDEBAR_WIDTH, g_cur_h - DETAILS_HEIGHT - 96)) {
                    int clk = (ev.y - 96) / 26;
                    if (clk >= 0 && clk < g_filtered_count) {
                        if (g_selected == clk) open_item();
                        else g_selected = clk;
                        render_explorer(win, g_cur_w, g_cur_h);
                    }
                } else if (ev.y > g_cur_h - DETAILS_HEIGHT) {
                    if (velo_ui_in_rect(ev.x, ev.y, g_cur_w - 130, g_cur_h - DETAILS_HEIGHT + 22, 110, 28)) {
                        open_item();
                        render_explorer(win, g_cur_w, g_cur_h);
                    }
                }
            }
        }

        // 3. KEYBOARD
        if (st == 1 && ev.type == VELO_EV_KEY) {
            if (ev.key == '\n') {
                open_item();
                render_explorer(win, g_cur_w, g_cur_h);
            } else if (g_search_focused) {
                if (ev.key == '\b' && g_search_len > 0) {
                    g_search[--g_search_len] = '\0';
                    apply_filter();
                    render_explorer(win, g_cur_w, g_cur_h);
                } else if (ev.key >= 32 && ev.key <= 126 && g_search_len < 20) {
                    g_search[g_search_len++] = ev.key;
                    g_search[g_search_len] = '\0';
                    apply_filter();
                    render_explorer(win, g_cur_w, g_cur_h);
                }
            }
        }
    }

    return 0;
}
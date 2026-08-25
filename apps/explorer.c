#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <velo/window.h>

#define MAX_FILES 32

static VeloDirEntry g_files[MAX_FILES];
static int g_file_count = 0;
static VeloDirEntry g_filtered[MAX_FILES];
static int g_filtered_count = 0;

static int g_selected = 0;
static char g_path[128] = "/";

static char g_search[32] = "";
static int g_search_len = 0;
static int g_search_focused = 0;

// Dynamische Fenstermaße vom OS
static int g_cur_w = 590;
static int g_cur_h = 320;

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
    if (g_selected >= g_filtered_count) g_selected = g_filtered_count > 0 ? 0 : -1;
}

static void load_dir(const char *path) {
    memset(g_files, 0, sizeof(g_files));
    g_file_count = velo_list_dir(path, g_files, MAX_FILES);
    if (g_file_count < 0) g_file_count = 0;
    g_selected = g_file_count > 0 ? 0 : -1;
    apply_filter();
}

static void format_date(UINT16 d, char *b) {
    if (!d) { sprintf(b, "12.03.2025"); return; }
    sprintf(b, "%02d.%02d.%04d", d & 0x1F, (d >> 5) & 0x0F, ((d >> 9) & 0x7F) + 1980);
}

static void format_sz(UINT32 s, int is_dir, char *b) {
    if (is_dir) sprintf(b, "<Ordner>");
    else if (s < 1024) sprintf(b, "%u B", (unsigned int)s);
    else if (s < 1048576) sprintf(b, "%u KB", (unsigned int)(s / 1024));
    else sprintf(b, "%u MB", (unsigned int)(s / 1048576));
}

static int is_exe(const char *n) {
    size_t l = strlen(n);
    return (l >= 4 && (!strcmp(n + l - 4, ".BIN") || !strcmp(n + l - 4, ".EFI")));
}

/* 
 * Responsive Rendering:
 * Alle Breiten und Höhen werden anhand der aktuellen Fenstermaße (w, h) berechnet
 */
static void render_explorer(velo_window_t win, int w, int h) {
    velo_window_clear(win);

    // 1. Adressleiste (dehnt sich aus) & Suchfeld (rechts angedockt)
    char full_path[192];
    sprintf(full_path, "Computer > Datentraeger (C:)%s", g_path);
    int addr_w = w - 160;
    if (addr_w < 180) addr_w = 180;
    velo_ui_addressbar(win, 6, 5, addr_w, full_path);
    velo_ui_searchbox(win, w - 146, 5, 140, g_search, g_search_focused);

    // 2. Toolbar über die volle Fensterbreite
    velo_ui_toolbar(win, 33, w, "Organisieren v  |  Oeffnen  |  Freigeben fuer v  |  Neuer Ordner");

    // 3. Sidebar links (Höhe spannt sich bis zur Details-Pane)
    int sidebar_h = h - 56 - 44;
    if (sidebar_h < 100) sidebar_h = 100;
    velo_ui_sidebar_panel(win, 56, 165, sidebar_h);
    velo_window_draw_text_colored(win, "v Favoriten", 10, 62, 0x002563EB);
    velo_ui_sidebar_item(win, 80, 165, " * Desktop", !strcmp(g_path, "/Users/Desktop"));
    velo_ui_sidebar_item(win, 102, 165, " = Dokumente", !strcmp(g_path, "/Users/Documents"));

    velo_window_draw_text_colored(win, "v Computer", 10, 126, 0x002563EB);
    velo_ui_sidebar_item(win, 144, 165, " [C:] Datentraeger", !strcmp(g_path, "/"));

    velo_window_draw_text_colored(win, "v Systemordner", 10, 168, 0x002563EB);
    velo_ui_sidebar_item(win, 186, 165, " /Windows", !strcmp(g_path, "/Windows"));
    velo_ui_sidebar_item(win, 208, 165, " /Program Files", !strcmp(g_path, "/Program Files"));
    velo_ui_sidebar_item(win, 230, 165, " /Users", !strcmp(g_path, "/Users"));

    // 4. Tabellen-Header & Dateien (füllt den gesamten rechten Raum)
    int table_w = w - 165;
    velo_ui_table_header(win, 165, 56, table_w);
    velo_window_draw_text_colored(win, "Name ^", 210, 59, 0x00475569);
    velo_window_draw_text_colored(win, "Aenderungsdatum", 350, 59, 0x00475569);
    velo_window_draw_text_colored(win, "Typ", w > 700 ? 510 : 440, 59, 0x00475569);
    velo_window_draw_text_colored(win, "Groesse", w - 75, 59, 0x00475569);

    int max_rows = (sidebar_h - 26) / 24;
    int row_y = 82;
    for (int i = 0; i < g_filtered_count && i < max_rows; i++) {
        char d_str[16], s_str[16];
        format_date(g_filtered[i].date, d_str);
        format_sz(g_filtered[i].size, g_filtered[i].is_dir, s_str);

        const char *icon = g_filtered[i].is_dir ? "[DIR]" : (is_exe(g_filtered[i].name) ? "[EXE]" : "[DOC]");
        const char *type = g_filtered[i].is_dir ? "Dateiordner" : (is_exe(g_filtered[i].name) ? "Anwendung" : "Datei");

        velo_ui_file_row(win, 165, row_y, table_w, icon, g_filtered[i].name, d_str, type, s_str, i == g_selected);
        row_y += 24;
    }

    if (!g_filtered_count) {
        velo_window_draw_text_colored(win, "Dieser Ordner ist leer.", 240, 120, 0x0064748B);
    }

    // 5. Details Pane fest am unteren Rand verankert
    int details_y = h - 44;
    if (g_selected >= 0 && g_selected < g_filtered_count) {
        VeloDirEntry *cur = &g_filtered[g_selected];
        char sz[16], meta[64];
        format_sz(cur->size, cur->is_dir, sz);
        sprintf(meta, "%s  |  Groesse: %s  |  Status: Bereit", cur->is_dir ? "Ordner" : "FAT32 Datei", sz);

        const char *btn = cur->is_dir ? "> Oeffnen" : (is_exe(cur->name) ? "> Starten" : "> Ansehen");
        const char *icn = cur->is_dir ? "[DIR]" : "[APP]";
        velo_ui_details_pane(win, details_y, w, 44, icn, cur->name, meta, btn);
    } else {
        char stat[64];
        sprintf(stat, "%d Objekt(e) auf Datentraeger geladen", g_file_count);
        velo_ui_details_pane(win, details_y, w, 44, "[HDD]", "Lokaler Datentraeger (C:)", stat, NULL);
    }

    velo_window_redraw();
}

static void open_item(void) {
    if (g_selected < 0 || g_selected >= g_filtered_count) return;
    VeloDirEntry *cur = &g_filtered[g_selected];

    if (cur->is_dir) {
        if (!strcmp(g_path, "/")) sprintf(g_path, "/%s", cur->name);
        else {
            size_t l = strlen(g_path);
            sprintf(g_path + l, "/%s", cur->name);
        }
        load_dir(g_path);
    } else if (is_exe(cur->name)) {
        velo_exec(cur->name);
    }
}

static void nav_up(void) {
    if (!strcmp(g_path, "/")) return;
    char *s = strrchr(g_path, '/');
    if (s && s != g_path) *s = '\0';
    else strcpy(g_path, "/");
    load_dir(g_path);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    load_dir(g_path);

    velo_window_t win = velo_window_create("Velo Explorer", g_cur_w, g_cur_h);
    if (win < 0) return 1;

    // Echte Fenstermaße vom OS erfragen
    UINT64 sz = velo_syscall(SYS_GET_WIN_SIZE, (UINT64)win, 0, 0, 0);
    if (sz) {
        g_cur_w = (int)(sz >> 32);
        g_cur_h = (int)(sz & 0xFFFFFFFF);
    }

    render_explorer(win, g_cur_w, g_cur_h);

    velo_event_t ev;
    while (1) {
        int st = velo_poll_event(win, &ev);
        if (st == -1) break;

        // 1. RESIZE EVENT (Vom OS bei Maximieren / Wiederherstellen)
        if (st == 1 && ev.type == 3) {
            g_cur_w = ev.x;
            g_cur_h = ev.y;
            render_explorer(win, g_cur_w, g_cur_h);
        }

        // 2. MAUSKLICKS
        if (st == 1 && ev.type == 1) {
            // Zurück-Button
            if (ev.x >= 6 && ev.x <= 28 && ev.y >= 6 && ev.y <= 28) { 
                nav_up(); 
                render_explorer(win, g_cur_w, g_cur_h); 
            }

            // Suchleiste rechts oben
            if (ev.x >= g_cur_w - 146 && ev.x <= g_cur_w - 6 && ev.y >= 5 && ev.y <= 29) { 
                g_search_focused = 1; 
                render_explorer(win, g_cur_w, g_cur_h); 
            } else if (g_search_focused) { 
                g_search_focused = 0; 
                render_explorer(win, g_cur_w, g_cur_h); 
            }

            // Sidebar Klicks
            if (ev.x >= 0 && ev.x <= 165 && ev.y >= 78 && ev.y <= g_cur_h - 44) {
                if (ev.y <= 96) strcpy(g_path, "/Users/Desktop");
                else if (ev.y <= 118) strcpy(g_path, "/Users/Documents");
                else if (ev.y <= 160) strcpy(g_path, "/");
                else if (ev.y <= 200) strcpy(g_path, "/Windows");
                else if (ev.y <= 222) strcpy(g_path, "/Program Files");
                else strcpy(g_path, "/Users");
                load_dir(g_path);
                render_explorer(win, g_cur_w, g_cur_h);
            }

            // Dateiliste anklicken
            if (ev.x >= 166 && ev.x <= g_cur_w && ev.y >= 80 && ev.y <= g_cur_h - 44) {
                int clk = (ev.y - 80) / 24;
                if (clk >= 0 && clk < g_filtered_count) {
                    if (g_selected == clk) open_item();
                    else g_selected = clk;
                    render_explorer(win, g_cur_w, g_cur_h);
                }
            }

            // Aktionsbutton rechts unten
            if (ev.x >= g_cur_w - 140 && ev.x <= g_cur_w - 10 && ev.y >= g_cur_h - 36 && ev.y <= g_cur_h - 8) {
                open_item();
                render_explorer(win, g_cur_w, g_cur_h);
            }
        }

        // 3. TASTATUR
        if (st == 1 && ev.type == 2) {
            if (ev.key == '\n') { 
                open_item(); 
                render_explorer(win, g_cur_w, g_cur_h); 
            } else if (g_search_focused) {
                if (ev.key == '\b' && g_search_len > 0) {
                    g_search[--g_search_len] = '\0';
                    apply_filter();
                    render_explorer(win, g_cur_w, g_cur_h);
                } else if (ev.key >= 32 && ev.key <= 126 && g_search_len < 16) {
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
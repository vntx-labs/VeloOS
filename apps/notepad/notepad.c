#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <velo/window.h>
#include <velo/syscall.h>
#include <velo/net.h>

#define MAX_TEXT_LEN 65536

static char g_text[MAX_TEXT_LEN];
static int  g_text_len = 0;
static int  g_cursor = 0;
static int  g_scroll_line = 0;
static int  g_is_dirty = 0;

static char g_current_path[128] = "";
static char g_file_title[64] = "Unbenannt";

static int g_win_w = 640;
static int g_win_h = 420;

// Menü-Status
static int g_menu_open = 0; // 1 = Datei, 2 = Bearbeiten
static int g_word_wrap = 1;

// Modale Dialoge (1 = Speichern unter, 2 = Datei öffnen)
static int g_dialog_mode = 0;
static char g_dialog_input[128] = "";
static int g_dialog_cursor = 0;
static int g_dialog_len = 0;

static void safe_strcpy(char *dst, const char *src, size_t max_len) {
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

static void get_cursor_line_col(int *out_line, int *out_col) {
    int line = 1;
    int col = 1;
    for (int i = 0; i < g_cursor && i < g_text_len; i++) {
        if (g_text[i] == '\n') {
            line++;
            col = 1;
        } else {
            col++;
        }
    }
    *out_line = line;
    *out_col = col;
}

static void load_file(const char *path) {
    if (!path || !path[0]) return;
    memset(g_text, 0, sizeof(g_text));
    int bytes = velo_read_file(path, g_text, sizeof(g_text) - 1);
    if (bytes >= 0) {
        g_text_len = bytes;
        g_text[bytes] = '\0';
        safe_strcpy(g_current_path, path, sizeof(g_current_path));
        const char *slash = strrchr(path, '/');
        if (!slash) slash = strrchr(path, '\\');
        safe_strcpy(g_file_title, slash ? slash + 1 : path, sizeof(g_file_title));
    } else {
        g_text_len = 0;
        g_text[0] = '\0';
        safe_strcpy(g_file_title, "Neu.txt", sizeof(g_file_title));
    }
    g_cursor = 0;
    g_scroll_line = 0;
    g_is_dirty = 0;
}

static void save_file(const char *path) {
    if (!path || !path[0]) return;
    velo_write_file(path, g_text, (UINT32)g_text_len);
    safe_strcpy(g_current_path, path, sizeof(g_current_path));
    const char *slash = strrchr(path, '/');
    if (!slash) slash = strrchr(path, '\\');
    safe_strcpy(g_file_title, slash ? slash + 1 : path, sizeof(g_file_title));
    g_is_dirty = 0;
}

static void render_notepad(velo_window_t win, int w, int h) {
    velo_window_draw_rect_color(win, 0, 0, w, h, 0x00FFFFFF);

    // 1. Menüleiste oben
    velo_window_draw_gradient(win, 0, 0, w, 24, 0x00F8FAFC, 0x00E2E8F0);
    velo_window_draw_rect_color(win, 0, 24, w, 1, 0x00CBD5E1);

    velo_window_draw_text_colored(win, "Datei", 10, 4, (g_menu_open == 1) ? 0x000284C7 : 0x000F172A);
    velo_window_draw_text_colored(win, "Bearbeiten", 60, 4, (g_menu_open == 2) ? 0x000284C7 : 0x000F172A);
    velo_window_draw_text_colored(win, "Format", 145, 4, 0x000F172A);
    velo_window_draw_text_colored(win, "Hilfe", 205, 4, 0x000F172A);

    // 2. Statusleiste unten
    int status_y = h - 22;
    velo_window_draw_gradient(win, 0, status_y, w, 22, 0x00F8FAFC, 0x00E2E8F0);
    velo_window_draw_rect_color(win, 0, status_y, w, 1, 0x00CBD5E1);

    int cur_line = 1, cur_col = 1;
    get_cursor_line_col(&cur_line, &cur_col);

    char stat_pos[64];
    sprintf(stat_pos, "  Zeile %d, Spalte %d", cur_line, cur_col);
    velo_window_draw_text_colored(win, stat_pos, 10, status_y + 3, 0x00475569);

    char stat_chars[48];
    sprintf(stat_chars, "%d Zeichen", g_text_len);
    velo_window_draw_text_colored(win, stat_chars, w - 230, status_y + 3, 0x00475569);
    velo_window_draw_text_colored(win, "Windows (CRLF)  UTF-8", w - 120, status_y + 3, 0x00475569);

    velo_window_draw_rect_color(win, w - 240, status_y + 2, 1, 18, 0x00CBD5E1);
    velo_window_draw_rect_color(win, w - 130, status_y + 2, 1, 18, 0x00CBD5E1);

    // 3. Textbereich
    int edit_x = 6;
    int edit_y = 28;
    int edit_w = w - 12;

    int line_height = 18;
    int char_width = 8;
    int max_chars_per_line = (edit_w - 12) / char_width;
    if (max_chars_per_line < 10) max_chars_per_line = 10;

    int cur_x = edit_x + 4;
    int cur_y = edit_y + 4;
    int cur_screen_line = 0;

    int i = 0;
    int line_char_count = 0;

    while (i <= g_text_len) {
        if (i == g_cursor && cur_screen_line >= g_scroll_line && cur_y + line_height <= status_y) {
            velo_window_draw_rect_color(win, cur_x, cur_y, 2, 16, 0x000F172A);
        }

        if (i == g_text_len) break;

        char c = g_text[i];

        if (c == '\n') {
            if (cur_screen_line >= g_scroll_line && cur_y + line_height <= status_y) {
                cur_y += line_height;
            }
            cur_screen_line++;
            cur_x = edit_x + 4;
            line_char_count = 0;
            i++;
            continue;
        }

        if (g_word_wrap && line_char_count >= max_chars_per_line) {
            if (cur_screen_line >= g_scroll_line && cur_y + line_height <= status_y) {
                cur_y += line_height;
            }
            cur_screen_line++;
            cur_x = edit_x + 4;
            line_char_count = 0;
        }

        if (cur_screen_line >= g_scroll_line && cur_y + line_height <= status_y) {
            char glyph[2] = {c, '\0'};
            velo_window_draw_text_colored(win, glyph, cur_x, cur_y, 0x000F172A);
            cur_x += char_width;
        }

        line_char_count++;
        i++;
    }

    // 4. Dropdown-Menüs (überlagern den Text)
    if (g_menu_open == 1) {
        int mx = 10, my = 24, mw = 160, mh = 130;
        velo_window_draw_rect_color(win, mx + 2, my + 2, mw, mh, 0x0064748B);
        velo_window_draw_gradient(win, mx, my, mw, mh, 0x00FFFFFF, 0x00F8FAFC);
        velo_window_draw_rect_color(win, mx, my, mw, 1, 0x0094A3B8);
        velo_window_draw_rect_color(win, mx, my + mh - 1, mw, 1, 0x0094A3B8);
        velo_window_draw_rect_color(win, mx, my, 1, mh, 0x0094A3B8);
        velo_window_draw_rect_color(win, mx + mw - 1, my, 1, mh, 0x0094A3B8);

        velo_window_draw_text_colored(win, "Neu", mx + 12, my + 8, 0x000F172A);
        velo_window_draw_text_colored(win, "Oeffnen...", mx + 12, my + 30, 0x000F172A);
        velo_window_draw_text_colored(win, "Speichern", mx + 12, my + 52, 0x000F172A);
        velo_window_draw_text_colored(win, "Speichern unter...", mx + 12, my + 74, 0x000F172A);
        velo_window_draw_rect_color(win, mx + 8, my + 96, mw - 16, 1, 0x00E2E8F0);
        velo_window_draw_text_colored(win, "Beenden", mx + 12, my + 104, 0x00DC2626);
    } else if (g_menu_open == 2) {
        int mx = 60, my = 24, mw = 150, mh = 110;
        velo_window_draw_rect_color(win, mx + 2, my + 2, mw, mh, 0x0064748B);
        velo_window_draw_gradient(win, mx, my, mw, mh, 0x00FFFFFF, 0x00F8FAFC);
        velo_window_draw_rect_color(win, mx, my, mw, 1, 0x0094A3B8);
        velo_window_draw_rect_color(win, mx, my + mh - 1, mw, 1, 0x0094A3B8);
        velo_window_draw_rect_color(win, mx, my, 1, mh, 0x0094A3B8);
        velo_window_draw_rect_color(win, mx + mw - 1, my, 1, mh, 0x0094A3B8);

        velo_window_draw_text_colored(win, "Ausschneiden", mx + 12, my + 8, 0x000F172A);
        velo_window_draw_text_colored(win, "Kopieren", mx + 12, my + 30, 0x000F172A);
        velo_window_draw_text_colored(win, "Einfuegen", mx + 12, my + 52, 0x000F172A);
        velo_window_draw_rect_color(win, mx + 8, my + 74, mw - 16, 1, 0x00E2E8F0);
        velo_window_draw_text_colored(win, "Alles loeschen", mx + 12, my + 82, 0x00DC2626);
    }

    // 5. Modaler Dialog (überlagert das gesamte Fenster)
    if (g_dialog_mode == 1 || g_dialog_mode == 2) {
        int dlg_w = 440, dlg_h = 160;
        int dlg_x = (w - dlg_w) / 2, dlg_y = (h - dlg_h) / 2;
        velo_ui_draw_modal_dialog(win, dlg_x, dlg_y, dlg_w, dlg_h,
            (g_dialog_mode == 1) ? "Speichern unter" : "Datei oeffnen",
            "Dateipfad (z. B. /Users/Desktop/Notiz.txt):",
            g_dialog_input, g_dialog_cursor,
            (g_dialog_mode == 1) ? "Speichern" : "Oeffnen", "Abbrechen");
    }

    velo_window_redraw();
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    char start_file[128];
    memset(start_file, 0, sizeof(start_file));
    int bytes = velo_read_file("/VeloOS/System32/NOTEPAD_FILE.DAT", start_file, sizeof(start_file) - 1);
    if (bytes > 0 && start_file[0]) {
        start_file[bytes] = '\0';
        velo_delete_file("/VeloOS/System32/NOTEPAD_FILE.DAT");
        load_file(start_file);
    } else {
        safe_strcpy(g_text, "Willkommen im Velo Editor!\nHier koennen Sie beliebig Texte bearbeiten und speichern.\n", sizeof(g_text));
        g_text_len = (int)strlen(g_text);
        g_cursor = g_text_len;
        safe_strcpy(g_current_path, "/Users/Desktop/Notiz.txt", sizeof(g_current_path));
        safe_strcpy(g_file_title, "Notiz.txt", sizeof(g_file_title));
    }

    velo_window_t win = velo_window_create("Editor (Notepad)", g_win_w, g_win_h);
    if (win < 0) return 0;

    g_win_w = velo_window_get_width(win);
    g_win_h = velo_window_get_height(win);

    render_notepad(win, g_win_w, g_win_h);

    velo_event_t ev;
    while (1) {
        int st = velo_poll_event(win, &ev);
        if (st == -1) break;

        if (st == 0) {
            velo_thread_sleep(1);
            continue;
        }

        if (st == 1 && ev.type == VELO_EV_RESIZE) {
            g_win_w = ev.x;
            g_win_h = ev.y;
            render_notepad(win, g_win_w, g_win_h);
            continue;
        }

        // ==========================================
        // MAUS-KLICK & POSITIONIERUNG
        // ==========================================
        if (st == 1 && ev.type == VELO_EV_CLICK) {
            // 1. Klick in Dialog-Eingabefeld oder Buttons
            if (g_dialog_mode) {
                int dlg_w = 440, dlg_h = 160;
                int dlg_x = (g_win_w - dlg_w) / 2, dlg_y = (g_win_h - dlg_h) / 2;
                int in_x = dlg_x + 20, in_y = dlg_y + 68, in_w = dlg_w - 40;
                int ok_x = dlg_x + dlg_w - 170, ok_y = dlg_y + dlg_h - 40;
                int can_x = dlg_x + dlg_w - 90;

                // Klick direkt in das Eingabefeld des Dialogs platziert Cursor!
                if (velo_ui_in_rect(ev.x, ev.y, in_x, in_y, in_w, 28)) {
                    int c = (ev.x - (in_x + 8) + 4) / 8;
                    if (c < 0) c = 0;
                    if (c > g_dialog_len) c = g_dialog_len;
                    g_dialog_cursor = c;
                    render_notepad(win, g_win_w, g_win_h);
                    continue;
                }

                if (velo_ui_in_rect(ev.x, ev.y, ok_x, ok_y, 70, 26)) {
                    if (g_dialog_mode == 1) save_file(g_dialog_input);
                    else if (g_dialog_mode == 2) load_file(g_dialog_input);
                    g_dialog_mode = 0;
                } else if (velo_ui_in_rect(ev.x, ev.y, can_x, ok_y, 70, 26)) {
                    g_dialog_mode = 0;
                }
                render_notepad(win, g_win_w, g_win_h);
                continue;
            }

            // 2. Klick in die Menüleiste
            if (ev.y <= 24) {
                if (ev.x >= 10 && ev.x <= 50) g_menu_open = (g_menu_open == 1) ? 0 : 1;
                else if (ev.x >= 60 && ev.x <= 135) g_menu_open = (g_menu_open == 2) ? 0 : 2;
                else if (ev.x >= 145 && ev.x <= 195) { g_word_wrap = !g_word_wrap; g_menu_open = 0; }
                else g_menu_open = 0;
                render_notepad(win, g_win_w, g_win_h);
                continue;
            }

            // 3. Klick in aufgeklapptes Menü
            if (g_menu_open == 1) {
                int mx = 10, my = 24;
                if (velo_ui_in_rect(ev.x, ev.y, mx, my, 160, 130)) {
                    int opt = (ev.y - my) / 22;
                    g_menu_open = 0;
                    if (opt == 0) {
                        g_text[0] = '\0'; g_text_len = 0; g_cursor = 0;
                        safe_strcpy(g_file_title, "Unbenannt", sizeof(g_file_title));
                        g_current_path[0] = '\0'; g_is_dirty = 0;
                    } else if (opt == 1) {
                        g_dialog_mode = 2;
                        safe_strcpy(g_dialog_input, "/Users/Desktop/", sizeof(g_dialog_input));
                        g_dialog_len = (int)strlen(g_dialog_input);
                        g_dialog_cursor = g_dialog_len;
                    } else if (opt == 2) {
                        if (g_current_path[0]) save_file(g_current_path);
                        else {
                            g_dialog_mode = 1;
                            safe_strcpy(g_dialog_input, "/Users/Desktop/Dokument.txt", sizeof(g_dialog_input));
                            g_dialog_len = (int)strlen(g_dialog_input);
                            g_dialog_cursor = g_dialog_len;
                        }
                    } else if (opt == 3) {
                        g_dialog_mode = 1;
                        safe_strcpy(g_dialog_input, g_current_path[0] ? g_current_path : "/Users/Desktop/Dokument.txt", sizeof(g_dialog_input));
                        g_dialog_len = (int)strlen(g_dialog_input);
                        g_dialog_cursor = g_dialog_len;
                    } else if (opt >= 4) {
                        exit(0);
                    }
                    render_notepad(win, g_win_w, g_win_h);
                    continue;
                } else {
                    g_menu_open = 0;
                }
            }

            g_menu_open = 0;

            // 4. Klick in den Haupt-Textbereich
            if (ev.y >= 28 && ev.y < g_win_h - 22) {
                int edit_x = 6;
                int edit_y = 28;
                int max_chars_per_line = (g_win_w - 24) / 8;
                
                g_cursor = velo_text_calc_cursor(g_text, g_text_len, 
                                                 ev.x, ev.y, 
                                                 edit_x + 4, edit_y + 4, 
                                                 8, 18, 
                                                 max_chars_per_line, g_word_wrap);
            }

            render_notepad(win, g_win_w, g_win_h);
        }

        // ==========================================
        // TASTATUR-EINGABEN
        // ==========================================
        if (st == 1 && ev.type == VELO_EV_KEY) {
            // Dialog Tastatursteuerung (Pfeiltasten, Pos1, Ende, Entf, Backspace)
            if (g_dialog_mode) {
                if (ev.key == '\n') {
                    if (g_dialog_mode == 1) save_file(g_dialog_input);
                    else if (g_dialog_mode == 2) load_file(g_dialog_input);
                    g_dialog_mode = 0;
                } else if (ev.key == (char)0x1B) {
                    g_dialog_mode = 0;
                } else if (ev.key == (char)0x84) { // LINKS
                    if (g_dialog_cursor > 0) g_dialog_cursor--;
                } else if (ev.key == (char)0x85) { // RECHTS
                    if (g_dialog_cursor < g_dialog_len) g_dialog_cursor++;
                } else if (ev.key == (char)0x86) { // POS1
                    g_dialog_cursor = 0;
                } else if (ev.key == (char)0x87) { // ENDE
                    g_dialog_cursor = g_dialog_len;
                } else if (ev.key == (char)0x88 || (unsigned char)ev.key == 0x7F) { // ENTF
                    if (g_dialog_cursor < g_dialog_len) {
                        for (int k = g_dialog_cursor; k < g_dialog_len; k++) g_dialog_input[k] = g_dialog_input[k + 1];
                        g_dialog_len--;
                    }
                } else if (ev.key == '\b') { // BACKSPACE
                    if (g_dialog_cursor > 0) {
                        for (int k = g_dialog_cursor - 1; k < g_dialog_len; k++) g_dialog_input[k] = g_dialog_input[k + 1];
                        g_dialog_len--; g_dialog_cursor--;
                    }
                } else if ((unsigned char)ev.key >= 32 && g_dialog_len < 120) {
                    for (int k = g_dialog_len; k >= g_dialog_cursor; k--) g_dialog_input[k + 1] = g_dialog_input[k];
                    g_dialog_input[g_dialog_cursor] = ev.key;
                    g_dialog_len++; g_dialog_cursor++;
                }
                render_notepad(win, g_win_w, g_win_h);
                continue;
            }

            int max_chars_per_line = (g_win_w - 24) / 8;

            if (ev.key == (char)0x82) { // PFEILTASTE RAUF
                g_cursor = velo_text_move_cursor_vertical(g_text, g_text_len, g_cursor, -1, max_chars_per_line, g_word_wrap);
            } else if (ev.key == (char)0x83) { // PFEILTASTE RUNTER
                g_cursor = velo_text_move_cursor_vertical(g_text, g_text_len, g_cursor, +1, max_chars_per_line, g_word_wrap);
            } else if (ev.key == (char)0x84) { // PFEILTASTE LINKS
                if (g_cursor > 0) g_cursor--;
            } else if (ev.key == (char)0x85) { // PFEILTASTE RECHTS
                if (g_cursor < g_text_len) g_cursor++;
            } else if (ev.key == (char)0x86) { // POS1 / HOME
                while (g_cursor > 0 && g_text[g_cursor - 1] != '\n') g_cursor--;
            } else if (ev.key == (char)0x87) { // ENDE
                while (g_cursor < g_text_len && g_text[g_cursor] != '\n') g_cursor++;
            } else if (ev.key == (char)0x88 || (unsigned char)ev.key == 0x7F) { // ENTF
                if (g_cursor < g_text_len) {
                    for (int k = g_cursor; k < g_text_len; k++) g_text[k] = g_text[k + 1];
                    g_text_len--;
                    g_is_dirty = 1;
                }
            } else if (ev.key == '\b') { // BACKSPACE
                if (g_cursor > 0) {
                    for (int k = g_cursor - 1; k < g_text_len; k++) g_text[k] = g_text[k + 1];
                    g_text_len--;
                    g_cursor--;
                    g_is_dirty = 1;
                }
            } else if (ev.key == '\t') { // TAB
                if (g_text_len + 4 < MAX_TEXT_LEN - 1) {
                    for (int t = 0; t < 4; t++) {
                        for (int k = g_text_len; k >= g_cursor; k--) g_text[k + 1] = g_text[k];
                        g_text[g_cursor] = ' ';
                        g_text_len++;
                        g_cursor++;
                    }
                    g_is_dirty = 1;
                }
            } else if (((unsigned char)ev.key >= 32 || ev.key == '\n') && g_text_len < MAX_TEXT_LEN - 1) {
                for (int k = g_text_len; k >= g_cursor; k--) g_text[k + 1] = g_text[k];
                g_text[g_cursor] = ev.key;
                g_text_len++;
                g_cursor++;
                g_is_dirty = 1;
            }

            render_notepad(win, g_win_w, g_win_h);
        }
    }

    return 0;
}
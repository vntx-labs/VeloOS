#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <velo/window.h>
#include <velo/syscall.h>
#include <velo/net.h>
#include <velo/icons.h>

#define MAX_TEXT_LEN 131072
#define SCROLLBAR_SIZE 16

static char g_text[MAX_TEXT_LEN];
static int  g_text_len = 0;
static int  g_cursor = 0;

static int  g_scroll_line = 0;
static int  g_scroll_col  = 0;
static int  g_word_wrap   = 0;

static int  g_total_lines = 1;
static int  g_max_line_len = 0;

static int  g_is_dirty = 0;
static char g_current_path[256] = "";
static char g_file_title[64] = "Unbenannt";

static int g_win_w = 700;
static int g_win_h = 460;

static int g_menu_open = 0;
static int g_gfd_active = 0;
static VeloFileDialog g_gfd;

static int g_dragging_vscroll = 0;
static int g_dragging_hscroll = 0;

static void safe_strcpy(char *dst, const char *src, size_t max_len) {
    if (!dst || max_len == 0) return;
    size_t i = 0;
    if (src) {
        while (src[i] && i < max_len - 1) { dst[i] = src[i]; i++; }
    }
    dst[i] = '\0';
}

static void recalculate_text_metrics(int visible_cols) {
    g_total_lines = 0;
    g_max_line_len = 0;

    int pos = 0;
    while (pos <= g_text_len) {
        int line_start = pos;
        while (pos < g_text_len && g_text[pos] != '\n') pos++;
        int raw_len = pos - line_start;

        if (raw_len > g_max_line_len) g_max_line_len = raw_len;

        if (!g_word_wrap || visible_cols <= 0 || raw_len <= visible_cols) {
            g_total_lines++;
        } else {
            int sub_start = line_start;
            while (sub_start < pos) {
                int rem = pos - sub_start;
                if (rem <= visible_cols) {
                    g_total_lines++;
                    break;
                }
                int break_len = visible_cols;
                int last_space = -1;
                for (int s = 0; s < visible_cols; s++) {
                    if (g_text[sub_start + s] == ' ') last_space = s;
                }
                if (last_space > 0) break_len = last_space + 1;
                g_total_lines++;
                sub_start += break_len;
            }
        }

        if (pos == g_text_len) {
            if (pos > 0 && g_text[pos - 1] == '\n') g_total_lines++;
            break;
        }
        pos++;
    }
    if (g_total_lines <= 0) g_total_lines = 1;
}

static void get_cursor_line_col(int *out_line, int *out_col) {
    int line = 0, col = 0;
    for (int i = 0; i < g_cursor && i < g_text_len; i++) {
        if (g_text[i] == '\n') {
            line++;
            col = 0;
        } else {
            col++;
        }
    }
    *out_line = line;
    *out_col = col;
}

// Stellt sicher, dass das Sichtfenster dem Cursor sowohl vertikal als auch horizontal folgt
static void ensure_cursor_visible(int visible_lines, int visible_cols) {
    int cur_line = 0, cur_col = 0;
    get_cursor_line_col(&cur_line, &cur_col);

    if (g_word_wrap) {
        g_scroll_col = 0;
    } else {
        if (cur_col < g_scroll_col) {
            g_scroll_col = cur_col;
        } else if (cur_col >= g_scroll_col + visible_cols - 2) {
            g_scroll_col = cur_col - visible_cols + 3;
        }
        if (g_scroll_col < 0) g_scroll_col = 0;
    }

    if (cur_line < g_scroll_line) {
        g_scroll_line = cur_line;
    } else if (cur_line >= g_scroll_line + visible_lines) {
        g_scroll_line = cur_line - visible_lines + 1;
    }
    if (g_scroll_line < 0) g_scroll_line = 0;
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
        safe_strcpy(g_file_title, "Unbenannt", sizeof(g_file_title));
    }
    g_cursor = 0;
    g_scroll_line = 0;
    g_scroll_col = 0;
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
    velo_window_clear(win);

    int status_y = h - 22;
    int edit_x = 4, edit_y = 26;
    int edit_w = w - 8 - SCROLLBAR_SIZE;
    int edit_h = status_y - edit_y - (g_word_wrap ? 0 : SCROLLBAR_SIZE);
    int visible_cols = edit_w / 8;
    int visible_lines = edit_h / 18;

    recalculate_text_metrics(visible_cols);
    ensure_cursor_visible(visible_lines, visible_cols);

    // 1. Menüleiste oben
    velo_window_draw_gradient(win, 0, 0, w, 24, 0x00F8FAFC, 0x00E2E8F0);
    velo_window_draw_rect_color(win, 0, 24, w, 1, 0x00CBD5E1);
    velo_window_draw_text_colored(win, "Datei", 10, 4, (g_menu_open == 1) ? 0x000284C7 : 0x000F172A);
    velo_window_draw_text_colored(win, "Bearbeiten", 60, 4, (g_menu_open == 2) ? 0x000284C7 : 0x000F172A);
    velo_window_draw_text_colored(win, "Format", 145, 4, (g_menu_open == 3) ? 0x000284C7 : 0x000F172A);

    // 2. Statusleiste unten
    velo_window_draw_gradient(win, 0, status_y, w, 22, 0x00F8FAFC, 0x00E2E8F0);
    velo_window_draw_rect_color(win, 0, status_y, w, 1, 0x00CBD5E1);

    int cur_l = 0, cur_c = 0;
    get_cursor_line_col(&cur_l, &cur_c);

    char stat_pos[64];
    sprintf(stat_pos, "  Zeile %d, Spalte %d", cur_l + 1, cur_c + 1);
    velo_window_draw_text_colored(win, stat_pos, 10, status_y + 3, 0x00475569);

    char stat_chars[64];
    sprintf(stat_chars, "%d Zeichen  |  %s", g_text_len, g_word_wrap ? "Umbruch: Ein" : "Umbruch: Aus");
    velo_window_draw_text_colored(win, stat_chars, w - 240, status_y + 3, 0x00475569);

    // 3. Schreibbereich
    velo_window_draw_rect_color(win, edit_x, edit_y, edit_w, edit_h, 0x00FFFFFF);

    // Text Render-Schleife mit flüssigem 2D-Auto-Scroll
    int pos = 0;
    int visual_line = 0;
    int cur_y = edit_y + 2;
    int cursor_rendered = 0;

    while (pos <= g_text_len && (cur_y + 18 <= edit_y + edit_h)) {
        int line_start = pos;
        while (pos < g_text_len && g_text[pos] != '\n') pos++;
        int raw_len = pos - line_start;

        int sub_start = line_start;
        do {
            int chunk_len = raw_len - (sub_start - line_start);
            int break_len = chunk_len;

            if (g_word_wrap && chunk_len > visible_cols && visible_cols > 0) {
                break_len = visible_cols;
                int last_sp = -1;
                for (int s = 0; s < visible_cols; s++) {
                    if (g_text[sub_start + s] == ' ') last_sp = s;
                }
                if (last_sp > 0) break_len = last_sp + 1;
            }

            if (visual_line >= g_scroll_line && (cur_y + 18 <= edit_y + edit_h)) {
                int draw_sc = g_word_wrap ? 0 : g_scroll_col;
                if (break_len > draw_sc) {
                    char buf[256];
                    int draw_chars = break_len - draw_sc;
                    if (draw_chars > visible_cols) draw_chars = visible_cols;
                    if (draw_chars > 255) draw_chars = 255;

                    memcpy(buf, &g_text[sub_start + draw_sc], draw_chars);
                    buf[draw_chars] = '\0';
                    velo_window_draw_text_colored(win, buf, edit_x + 4, cur_y, 0x000F172A);
                }

                // Cursor exakt an Zielposition
                if (!cursor_rendered && g_cursor >= sub_start && (g_cursor < sub_start + break_len || (g_cursor == sub_start + break_len && pos == g_text_len))) {
                    int c_offset = g_cursor - sub_start - draw_sc;
                    if (c_offset >= 0 && c_offset <= visible_cols) {
                        velo_window_draw_rect_color(win, edit_x + 4 + c_offset * 8, cur_y, 2, 16, 0x000F172A);
                    }
                    cursor_rendered = 1;
                }
                cur_y += 18;
            }

            sub_start += break_len;
            visual_line++;
        } while (g_word_wrap && sub_start < pos);

        if (pos == g_text_len) break;
        pos++;
    }

    // 4. Vertikale Scrollbar mit exakter Bounds-Begrenzung (kein Rausgehen aus dem Fenster)
    int vsb_x = w - SCROLLBAR_SIZE - 2;
    velo_window_draw_rect_color(win, vsb_x, edit_y, SCROLLBAR_SIZE, edit_h, 0x00F1F5F9);
    velo_window_draw_rect_color(win, vsb_x, edit_y, 1, edit_h, 0x00CBD5E1);

    int v_thumb_h = (g_total_lines > visible_lines) ? (visible_lines * edit_h) / g_total_lines : edit_h;
    if (v_thumb_h < 24) v_thumb_h = 24;
    if (v_thumb_h > edit_h) v_thumb_h = edit_h;

    int max_v_scroll = (g_total_lines > visible_lines) ? (g_total_lines - visible_lines) : 1;
    int v_thumb_y = edit_y + (g_scroll_line * (edit_h - v_thumb_h)) / max_v_scroll;

    if (v_thumb_y < edit_y) v_thumb_y = edit_y;
    if (v_thumb_y + v_thumb_h > edit_y + edit_h) v_thumb_y = edit_y + edit_h - v_thumb_h;

    velo_window_draw_gradient(win, vsb_x + 2, v_thumb_y, SCROLLBAR_SIZE - 4, v_thumb_h, 0x00E2E8F0, 0x00CBD5E1);
    velo_window_draw_rect_color(win, vsb_x + 2, v_thumb_y, SCROLLBAR_SIZE - 4, v_thumb_h, 0x0094A3B8);

    // 5. Horizontale Scrollbar mit exakter Bounds-Begrenzung
    if (!g_word_wrap) {
        int hsb_y = edit_y + edit_h;
        velo_window_draw_rect_color(win, edit_x, hsb_y, edit_w, SCROLLBAR_SIZE, 0x00F1F5F9);
        velo_window_draw_rect_color(win, edit_x, hsb_y, edit_w, 1, 0x00CBD5E1);

        int total_cols = (g_max_line_len > visible_cols) ? g_max_line_len : visible_cols;
        int h_thumb_w = (total_cols > visible_cols) ? (visible_cols * edit_w) / total_cols : edit_w;
        if (h_thumb_w < 24) h_thumb_w = 24;
        if (h_thumb_w > edit_w) h_thumb_w = edit_w;

        int max_h_scroll = (total_cols > visible_cols) ? (total_cols - visible_cols) : 1;
        int h_thumb_x = edit_x + (g_scroll_col * (edit_w - h_thumb_w)) / max_h_scroll;

        if (h_thumb_x < edit_x) h_thumb_x = edit_x;
        if (h_thumb_x + h_thumb_w > edit_x + edit_w) h_thumb_x = edit_x + edit_w - h_thumb_w;

        velo_window_draw_gradient(win, h_thumb_x, hsb_y + 2, h_thumb_w, SCROLLBAR_SIZE - 4, 0x00E2E8F0, 0x00CBD5E1);
        velo_window_draw_rect_color(win, h_thumb_x, hsb_y + 2, h_thumb_w, SCROLLBAR_SIZE - 4, 0x0094A3B8);
    }

    // 6. Menüleisten Dropdowns
    if (g_menu_open == 1) {
        int mx = 10, my = 24, mw = 170, mh = 134;
        velo_window_draw_rect_color(win, mx + 2, my + 2, mw, mh, 0x0064748B);
        velo_window_draw_gradient(win, mx, my, mw, mh, 0x00FFFFFF, 0x00F8FAFC);
        velo_window_draw_rect_color(win, mx, my, mw, 1, 0x0094A3B8);
        velo_window_draw_rect_color(win, mx, my + mh - 1, mw, 1, 0x0094A3B8);
        velo_window_draw_rect_color(win, mx, my, 1, mh, 0x0094A3B8);
        velo_window_draw_rect_color(win, mx + mw - 1, my, 1, mh, 0x0094A3B8);

        velo_window_draw_text_colored(win, "Neu            Ctrl+N", mx + 12, my + 8, 0x000F172A);
        velo_window_draw_text_colored(win, "Oeffnen...     Ctrl+O", mx + 12, my + 30, 0x000F172A);
        velo_window_draw_text_colored(win, "Speichern      Ctrl+S", mx + 12, my + 52, 0x000F172A);
        velo_window_draw_text_colored(win, "Speichern unter...", mx + 12, my + 74, 0x000F172A);
        velo_window_draw_rect_color(win, mx + 8, my + 96, mw - 16, 1, 0x00E2E8F0);
        velo_window_draw_text_colored(win, "Beenden", mx + 12, my + 106, 0x00DC2626);
    } else if (g_menu_open == 3) {
        int mx = 145, my = 24, mw = 180, mh = 44;
        velo_window_draw_rect_color(win, mx + 2, my + 2, mw, mh, 0x0064748B);
        velo_window_draw_gradient(win, mx, my, mw, mh, 0x00FFFFFF, 0x00F8FAFC);
        velo_window_draw_rect_color(win, mx, my, mw, 1, 0x0094A3B8);
        velo_window_draw_rect_color(win, mx, my + mh - 1, mw, 1, 0x0094A3B8);
        velo_window_draw_rect_color(win, mx, my, 1, mh, 0x0094A3B8);
        velo_window_draw_rect_color(win, mx + mw - 1, my, 1, mh, 0x0094A3B8);

        char wrap_lbl[32];
        sprintf(wrap_lbl, "[%c] Zeilenumbruch", g_word_wrap ? 'x' : ' ');
        velo_window_draw_text_colored(win, wrap_lbl, mx + 12, my + 14, 0x000F172A);
    }

    if (g_gfd_active) {
        velo_gfd_render(win, &g_gfd, w, h);
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
        safe_strcpy(g_text, "Willkommen im Velo Editor!\nHier koennen Sie Notizen schreiben.\n", sizeof(g_text));
        g_text_len = (int)strlen(g_text);
        g_cursor = g_text_len;
        safe_strcpy(g_current_path, "C:/Users/Desktop/Notiz.txt", sizeof(g_current_path));
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
        if (st == 0) { velo_thread_sleep(1); continue; }

        if (st == 1 && ev.type == VELO_EV_RESIZE) {
            g_win_w = ev.x;
            g_win_h = ev.y;
            render_notepad(win, g_win_w, g_win_h);
            continue;
        }

        // ==========================================================
        // KLASSISCHE MAUSRAD-SCROLLRICHTUNG (WIE IN WINDOWS)
        // ==========================================================
        if (st == 1 && ev.type == VELO_EV_SCROLL) {
            // Nach unten drehen (ev.scroll_y < 0) -> Text wandert nach oben (g_scroll_line nimmt zu)
            // Nach oben drehen (ev.scroll_y > 0) -> Text wandert nach unten (g_scroll_line nimmt ab)
            if (ev.scroll_y != 0) {
                g_scroll_line += ev.scroll_y * 3;
                if (g_scroll_line < 0) g_scroll_line = 0;
                if (g_scroll_line >= g_total_lines) g_scroll_line = g_total_lines - 1;
            }
            if (ev.scroll_x != 0 && !g_word_wrap) {
                g_scroll_col += ev.scroll_x * 4;
                if (g_scroll_col < 0) g_scroll_col = 0;
            }
            render_notepad(win, g_win_w, g_win_h);
            continue;
        }

        if (st == 1 && ev.type == VELO_EV_MOUSEUP) {
            g_dragging_vscroll = 0;
            g_dragging_hscroll = 0;
            continue;
        }

        // ==========================================================
        // MAUS-KLICKS & SCROLLBAR-DRAGGING
        // ==========================================================
        if (st == 1 && ev.type == VELO_EV_CLICK) {
            if (g_gfd_active) {
                int res = velo_gfd_handle_click(&g_gfd, g_win_w, g_win_h, ev.x, ev.y);
                if (res == GFD_ACTION_CONFIRM) {
                    char full_target[256];
                    velo_gfd_get_selected_path(&g_gfd, full_target, sizeof(full_target));
                    if (g_gfd_active == 1) save_file(full_target);
                    else if (g_gfd_active == 2) load_file(full_target);
                    g_gfd_active = 0;
                } else if (res == GFD_ACTION_CANCEL) {
                    g_gfd_active = 0;
                }
                render_notepad(win, g_win_w, g_win_h);
                continue;
            }

            int status_y = g_win_h - 22;
            int edit_x = 4, edit_y = 26;
            int edit_w = g_win_w - 8 - SCROLLBAR_SIZE;
            int edit_h = status_y - edit_y - (g_word_wrap ? 0 : SCROLLBAR_SIZE);

            // Vertikale Scrollbar
            int vsb_x = g_win_w - SCROLLBAR_SIZE - 2;
            if (velo_ui_in_rect(ev.x, ev.y, vsb_x, edit_y, SCROLLBAR_SIZE, edit_h)) {
                g_dragging_vscroll = 1;
                int max_v = (g_total_lines > (edit_h / 18)) ? (g_total_lines - (edit_h / 18)) : 1;
                g_scroll_line = ((ev.y - edit_y) * max_v) / edit_h;
                if (g_scroll_line < 0) g_scroll_line = 0;
                if (g_scroll_line >= g_total_lines) g_scroll_line = g_total_lines - 1;
                render_notepad(win, g_win_w, g_win_h);
                continue;
            }

            // Horizontale Scrollbar
            if (!g_word_wrap && velo_ui_in_rect(ev.x, ev.y, edit_x, edit_y + edit_h, edit_w, SCROLLBAR_SIZE)) {
                g_dragging_hscroll = 1;
                int total_c = (g_max_line_len > (edit_w / 8)) ? g_max_line_len : (edit_w / 8);
                int max_h = (total_c > (edit_w / 8)) ? (total_c - (edit_w / 8)) : 1;
                g_scroll_col = ((ev.x - edit_x) * max_h) / edit_w;
                if (g_scroll_col < 0) g_scroll_col = 0;
                render_notepad(win, g_win_w, g_win_h);
                continue;
            }

            // Menüleiste
            if (ev.y <= 24) {
                if (ev.x >= 10 && ev.x <= 50) g_menu_open = (g_menu_open == 1) ? 0 : 1;
                else if (ev.x >= 60 && ev.x <= 135) g_menu_open = (g_menu_open == 2) ? 0 : 2;
                else if (ev.x >= 145 && ev.x <= 200) g_menu_open = (g_menu_open == 3) ? 0 : 3;
                else g_menu_open = 0;
                render_notepad(win, g_win_w, g_win_h);
                continue;
            }

            // Dropdowns
            if (g_menu_open == 1) {
                int mx = 10, my = 24;
                if (velo_ui_in_rect(ev.x, ev.y, mx, my, 170, 134)) {
                    int opt = (ev.y - my) / 22;
                    g_menu_open = 0;
                    if (opt == 0) {
                        g_text[0] = '\0'; g_text_len = 0; g_cursor = 0;
                        safe_strcpy(g_file_title, "Unbenannt", sizeof(g_file_title));
                        g_current_path[0] = '\0'; g_is_dirty = 0;
                    } else if (opt == 1) {
                        velo_gfd_init(&g_gfd, GFD_MODE_OPEN, "C:/Users/Desktop");
                        g_gfd_active = 2;
                    } else if (opt == 2) {
                        if (g_current_path[0]) save_file(g_current_path);
                        else {
                            velo_gfd_init(&g_gfd, GFD_MODE_SAVE, "C:/Users/Desktop");
                            g_gfd_active = 1;
                        }
                    } else if (opt == 3) {
                        velo_gfd_init(&g_gfd, GFD_MODE_SAVE, "C:/Users/Desktop");
                        g_gfd_active = 1;
                    } else if (opt >= 4) {
                        exit(0);
                    }
                    render_notepad(win, g_win_w, g_win_h);
                    continue;
                }
            } else if (g_menu_open == 3) {
                int mx = 145, my = 24;
                if (velo_ui_in_rect(ev.x, ev.y, mx, my, 180, 44)) {
                    g_word_wrap = !g_word_wrap;
                    g_menu_open = 0;
                    render_notepad(win, g_win_w, g_win_h);
                    continue;
                }
            }
            g_menu_open = 0;

            // Klick in Textfeld (Cursor Positionierung)
            if (ev.y >= edit_y && ev.y < edit_y + edit_h && ev.x >= edit_x && ev.x < edit_x + edit_w) {
                int target_l = g_scroll_line + (ev.y - edit_y - 2) / 18;
                int target_c = (g_word_wrap ? 0 : g_scroll_col) + (ev.x - edit_x - 4 + 4) / 8;
                if (target_c < 0) target_c = 0;

                int pos = 0, v_line = 0, found = 0;
                while (pos <= g_text_len) {
                    int l_start = pos;
                    while (pos < g_text_len && g_text[pos] != '\n') pos++;
                    int r_len = pos - l_start;

                    int sub_start = l_start;
                    do {
                        int chunk = r_len - (sub_start - l_start);
                        int b_len = chunk;
                        if (g_word_wrap && chunk > (edit_w / 8) && (edit_w / 8) > 0) {
                            b_len = edit_w / 8;
                            int last_sp = -1;
                            for (int s = 0; s < (edit_w / 8); s++) {
                                if (g_text[sub_start + s] == ' ') last_sp = s;
                            }
                            if (last_sp > 0) b_len = last_sp + 1;
                        }

                        if (v_line == target_l) {
                            int chosen = (target_c < b_len) ? target_c : b_len;
                            g_cursor = sub_start + chosen;
                            found = 1;
                            break;
                        }
                        sub_start += b_len;
                        v_line++;
                    } while (g_word_wrap && sub_start < pos);

                    if (found || pos == g_text_len) break;
                    pos++;
                }
                if (!found) g_cursor = g_text_len;
            }
            render_notepad(win, g_win_w, g_win_h);
        }

        // ==========================================================
        // TASTATUR-EINGABE MIT AUTO-FOLLOW-SCROLLING
        // ==========================================================
        if (st == 1 && ev.type == VELO_EV_KEY) {
            if (g_gfd_active) {
                int res = velo_gfd_handle_key(&g_gfd, ev.key);
                if (res == GFD_ACTION_CONFIRM) {
                    char full_target[256];
                    velo_gfd_get_selected_path(&g_gfd, full_target, sizeof(full_target));
                    if (g_gfd_active == 1) save_file(full_target);
                    else if (g_gfd_active == 2) load_file(full_target);
                    g_gfd_active = 0;
                } else if (res == GFD_ACTION_CANCEL) {
                    g_gfd_active = 0;
                }
                render_notepad(win, g_win_w, g_win_h);
                continue;
            }

            if (ev.key == (char)0x84) { if (g_cursor > 0) g_cursor--; }
            else if (ev.key == (char)0x85) { if (g_cursor < g_text_len) g_cursor++; }
            else if (ev.key == (char)0x86) { while (g_cursor > 0 && g_text[g_cursor - 1] != '\n') g_cursor--; }
            else if (ev.key == (char)0x87) { while (g_cursor < g_text_len && g_text[g_cursor] != '\n') g_cursor++; }
            else if (ev.key == (char)0x88 || (unsigned char)ev.key == 0x7F) {
                if (g_cursor < g_text_len) {
                    for (int k = g_cursor; k < g_text_len; k++) g_text[k] = g_text[k + 1];
                    g_text_len--;
                    g_is_dirty = 1;
                }
            } else if (ev.key == '\b') {
                if (g_cursor > 0) {
                    for (int k = g_cursor - 1; k < g_text_len; k++) g_text[k] = g_text[k + 1];
                    g_text_len--;
                    g_cursor--;
                    g_is_dirty = 1;
                }
            } else if (ev.key == '\t') {
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
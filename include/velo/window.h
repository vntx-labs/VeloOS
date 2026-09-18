#ifndef _VELO_WINDOW_H
#define _VELO_WINDOW_H

#include <velo/syscall.h>
#include <velo/icons.h>

#ifdef __cplusplus
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
using std::size_t;
extern "C" {
#else
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#endif

typedef int velo_window_t;

// ====================================================
// ERWEITERTE KONTRAST-BERECHNUNG (ITU-R BT.709 LUMINANZ)
// ====================================================
static inline UINT32 velo_get_contrast_color(UINT32 bg_color) {
    unsigned int r = (bg_color >> 16) & 0xFF;
    unsigned int g = (bg_color >> 8) & 0xFF;
    unsigned int b = bg_color & 0xFF;
    unsigned int luminance = (r * 2126 + g * 7152 + b * 722) / 10000;
    return (luminance >= 135) ? 0x000F172A : 0x00F8FAFC;
}

// ==========================================
// BASIS-ZEICHEN-SYSCALLS (FENSTER)
// ==========================================
static inline velo_window_t velo_window_create(const char *title, int width, int height) {
    return (velo_window_t)velo_syscall(SYS_CREATE_WINDOW, (UINT64)title, (UINT64)width, (UINT64)height, 0);
}

static inline void velo_window_clear(velo_window_t win) {
    velo_syscall(SYS_CLEAR_WINDOW, (UINT64)win, 0, 0, 0);
}

static inline void velo_window_draw_rect(velo_window_t win, int x, int y, int w, int h) {
    UINT64 packed_pos = ((UINT64)(UINT32)x << 32) | (UINT32)y;
    UINT64 packed_dim = ((UINT64)(UINT32)w << 32) | (UINT32)h;
    velo_syscall(SYS_DRAW_RECT, (UINT64)win, packed_pos, packed_dim, 0);
}

static inline void velo_window_draw_rect_color(velo_window_t win, int x, int y, int w, int h, UINT32 color) {
    UINT64 packed_pos = ((UINT64)(UINT32)x << 32) | (UINT32)y;
    UINT64 packed_dim = ((UINT64)(UINT32)w << 32) | (UINT32)h;
    velo_syscall(SYS_DRAW_RECT_COL, (UINT64)win, packed_pos, packed_dim, (UINT64)color);
}

static inline void velo_window_draw_gradient(velo_window_t win, int x, int y, int w, int h, UINT32 top_col, UINT32 bot_col) {
    UINT64 packed_pos = ((UINT64)(UINT32)x << 32) | (UINT32)y;
    UINT64 packed_dim = ((UINT64)(UINT32)w << 32) | (UINT32)h;
    UINT64 packed_col = ((UINT64)top_col << 32) | (UINT32)bot_col;
    velo_syscall(SYS_DRAW_GRADIENT, (UINT64)win, packed_pos, packed_dim, packed_col);
}

static inline void velo_window_draw_text(velo_window_t win, const char *text, int x, int y) {
    velo_syscall(SYS_DRAW_TEXT, (UINT64)win, (UINT64)text, (UINT64)x, (UINT64)y);
}

static inline void velo_window_draw_text_colored(velo_window_t win, const char *text, int x, int y, UINT32 fg_col) {
    velo_syscall(SYS_DRAW_TEXT_COL, (UINT64)win, (UINT64)text, (UINT64)x, ((UINT64)y << 32) | (UINT32)fg_col);
}

static inline void velo_window_draw_text_auto(velo_window_t win, const char *text, int x, int y, UINT32 bg_color) {
    UINT32 contrast_fg = velo_get_contrast_color(bg_color);
    velo_window_draw_text_colored(win, text, x, y, contrast_fg);
}

static inline void velo_window_draw_icon(velo_window_t win, int icon_type, int x, int y, int size) {
    UINT64 packed_pos = ((UINT64)(UINT32)x << 32) | (UINT32)y;
    velo_syscall(SYS_DRAW_ICON, (UINT64)win, (UINT64)icon_type, packed_pos, (UINT64)size);
}

static inline int velo_poll_event(velo_window_t win, velo_event_t *ev) {
    return (int)velo_syscall(SYS_GET_EVENT, (UINT64)win, (UINT64)ev, 0, 0);
}

static inline void velo_window_redraw(void) {
    velo_syscall(SYS_MARK_DIRTY, 0, 0, 0, 0);
}

static inline void velo_window_close(velo_window_t win) {
    (void)win;
    velo_syscall(SYS_EXIT, 0, 0, 0, 0);
}

static inline int velo_window_get_width(velo_window_t win) {
    UINT64 sz = velo_syscall(SYS_GET_WIN_SIZE, (UINT64)win, 0, 0, 0);
    return sz ? (int)(sz >> 32) : 800;
}

static inline int velo_window_get_height(velo_window_t win) {
    UINT64 sz = velo_syscall(SYS_GET_WIN_SIZE, (UINT64)win, 0, 0, 0);
    return sz ? (int)(sz & 0xFFFFFFFF) : 600;
}

static inline int velo_ui_in_rect(int px, int py, int x, int y, int w, int h) {
    return (px >= x && px < x + w && py >= y && py < y + h);
}

// ====================================================
// UNIVERSELLE TEXTBOX & CURSOR STEUERUNG
// ====================================================
int velo_ui_textbox_handle_key(char *text, int max_len, int *cursor_pos, char key);
int velo_ui_textbox_handle_click(const char *text, int *cursor_pos, int click_x, int box_text_start_x, int char_w);

// ====================================================
// WINDOWS/macOS MEHRZEILEN-CURSOR BERECHNUNG (NOTEPAD)
// ====================================================
static inline int velo_text_calc_cursor(const char *text, int text_len, 
                                        int click_x, int click_y, 
                                        int start_x, int start_y, 
                                        int char_w, int line_h, 
                                        int max_chars_per_line, int word_wrap) {
    if (!text || text_len <= 0) return 0;
    if (click_y < start_y) return 0;

    int target_line = (click_y - start_y) / line_h;
    int target_col  = (click_x - start_x + (char_w / 2)) / char_w;
    if (target_col < 0) target_col = 0;

    int cur_line = 0;
    int cur_col = 0;

    for (int i = 0; i < text_len; i++) {
        if (cur_line == target_line) {
            if (cur_col == target_col || text[i] == '\n') {
                return i;
            }
        }

        if (text[i] == '\n') {
            if (cur_line == target_line) return i;
            cur_line++;
            cur_col = 0;
            continue;
        }

        if (word_wrap && max_chars_per_line > 0 && cur_col >= max_chars_per_line) {
            if (cur_line == target_line) return i;
            cur_line++;
            cur_col = 0;
        }

        cur_col++;
    }

    return text_len;
}

static inline int velo_text_move_cursor_vertical(const char *text, int text_len, 
                                                 int cur_pos, int direction, 
                                                 int max_chars_per_line, int word_wrap) {
    if (!text || text_len <= 0) return 0;

    int cur_line = 0;
    int cur_col = 0;

    for (int i = 0; i < cur_pos && i < text_len; i++) {
        if (text[i] == '\n') {
            cur_line++;
            cur_col = 0;
            continue;
        }
        if (word_wrap && max_chars_per_line > 0 && cur_col >= max_chars_per_line) {
            cur_line++;
            cur_col = 0;
        }
        cur_col++;
    }

    int target_line = cur_line + direction;
    if (target_line < 0) return 0;

    int target_col = cur_col;
    int line = 0;
    int col = 0;

    for (int i = 0; i < text_len; i++) {
        if (line == target_line) {
            if (col == target_col || text[i] == '\n') {
                return i;
            }
        }

        if (text[i] == '\n') {
            if (line == target_line) return i;
            line++;
            col = 0;
            continue;
        }

        if (word_wrap && max_chars_per_line > 0 && col >= max_chars_per_line) {
            if (line == target_line) return i;
            line++;
            col = 0;
        }

        col++;
    }

    if (direction > 0) return text_len;
    return 0;
}

// ==========================================
// DELEGIERTE UI WIDGET SYSCALLS (macOS Glass Style)
// ==========================================
static inline void velo_ui_draw_button(velo_window_t win, int x, int y, int w, int h, const char *label, UINT32 top_col, UINT32 bot_col, UINT32 border_col, UINT32 text_col) {
    (void)top_col; (void)bot_col; (void)border_col; (void)text_col;
    UINT64 packed_pos = ((UINT64)(UINT32)x << 32) | (UINT32)y;
    UINT64 packed_dim = ((UINT64)(UINT32)w << 32) | (UINT32)h;
    velo_syscall(SYS_DRAW_BUTTON, (UINT64)win, packed_pos, packed_dim, (UINT64)label);
}

static inline void velo_ui_draw_nav_btn(velo_window_t win, int x, int y, const char *symbol, int enabled, int is_hover) {
    (void)is_hover;
    UINT64 packed_pos = ((UINT64)(UINT32)x << 32) | (UINT32)y;
    velo_syscall(SYS_DRAW_NAV_BTN, (UINT64)win, packed_pos, (UINT64)enabled, (UINT64)symbol);
}

static inline void velo_ui_draw_addressbar(velo_window_t win, int x, int y, int w, const char *path) {
    UINT64 packed_pos = ((UINT64)(UINT32)x << 32) | (UINT32)y;
    velo_syscall(SYS_DRAW_ADDR_BAR, (UINT64)win, packed_pos, (UINT64)w, (UINT64)path);
}

static inline void velo_ui_draw_searchbox(velo_window_t win, int x, int y, int w, const char *query, int cursor_pos, int focused) {
    UINT64 packed_pos = ((UINT64)(UINT32)x << 32) | (UINT32)y;
    UINT64 packed_state = ((UINT64)(UINT32)w << 32) | ((UINT32)(cursor_pos & 0xFFFF)) | ((UINT32)(focused ? 1 : 0) << 16);
    velo_syscall(SYS_DRAW_SEARCHBOX, (UINT64)win, packed_pos, packed_state, (UINT64)query);
}

static inline void velo_ui_draw_command_bar(velo_window_t win, int y, int w) {
    velo_syscall(SYS_DRAW_COMMAND_BAR, (UINT64)win, (UINT64)y, (UINT64)w, 0);
}

static inline void velo_ui_draw_sidebar_item(velo_window_t win, int y, int w, const char *label, int is_selected) {
    UINT64 packed_dim = ((UINT64)(UINT32)y << 32) | (UINT32)w;
    velo_syscall(SYS_DRAW_SIDEBAR_ITM, (UINT64)win, packed_dim, (UINT64)is_selected, (UINT64)label);
}

static inline void velo_ui_draw_storage_bar(velo_window_t win, int x, int y, int w, int percent) {
    UINT64 packed_pos = ((UINT64)(UINT32)x << 32) | (UINT32)y;
    velo_syscall(SYS_DRAW_STORAGE_BAR, (UINT64)win, packed_pos, (UINT64)w, (UINT64)percent);
}

static inline void velo_ui_draw_modal_dialog(velo_window_t win, int x, int y, int w, int h, const char *title, const char *prompt, const char *text_val, int cursor_pos, const char *btn_ok, const char *btn_cancel) {
    UINT64 packed_pos = ((UINT64)(UINT32)x << 32) | (UINT32)y;
    UINT64 packed_dim = ((UINT64)(UINT32)w << 32) | (UINT32)h;
    velo_syscall(SYS_DRAW_DIALOG, (UINT64)win, packed_pos, packed_dim, (UINT64)title);

    UINT32 dlg_bg = 0x001E293B;
    UINT32 contrast_text = velo_get_contrast_color(dlg_bg);
    velo_window_draw_text_colored(win, prompt, x + 20, y + 44, contrast_text);

    int in_x = x + 20, in_y = y + 68, in_w = w - 40;
    velo_window_draw_gradient(win, in_x, in_y, in_w, 28, 0x001E293B, 0x000F172A);
    velo_window_draw_rect_color(win, in_x, in_y, in_w, 1, 0x0038BDF8);
    velo_window_draw_rect_color(win, in_x, in_y + 27, in_w, 1, 0x0038BDF8);
    velo_window_draw_rect_color(win, in_x, in_y, 1, 28, 0x0038BDF8);
    velo_window_draw_rect_color(win, in_x + in_w - 1, in_y, 1, 28, 0x0038BDF8);

    velo_window_draw_text_colored(win, text_val, in_x + 8, in_y + 6, 0x00FFFFFF);
    velo_window_draw_rect_color(win, in_x + 8 + cursor_pos * 8, in_y + 5, 1, 16, 0x0038BDF8);

    int ok_x = x + w - 170, ok_y = y + h - 40;
    velo_ui_draw_button(win, ok_x, ok_y, 70, 26, btn_ok, 0, 0, 0, 0);

    int can_x = x + w - 90;
    velo_ui_draw_button(win, can_x, ok_y, 70, 26, btn_cancel, 0, 0, 0, 0);
}

// ====================================================
// ADVANCED GRAPHICAL FILE DIALOG (GFD - macOS Style)
// ====================================================
#define GFD_MODE_OPEN   1
#define GFD_MODE_SAVE   2
#define GFD_MAX_ENTRIES 128

#define GFD_ACTION_NONE    0
#define GFD_ACTION_CONFIRM 1
#define GFD_ACTION_CANCEL  2

typedef struct {
    int mode;
    char current_path[256];
    char filename_input[64];
    int cursor_pos;
    VeloDirEntry entries[GFD_MAX_ENTRIES];
    int entry_count;
    int selected_idx;
    int scroll_offset;

    int filter_type;
    int show_extensions;

    int mkdir_active;
    char mkdir_input[32];
    int mkdir_cursor;
} VeloFileDialog;

static inline void velo_gfd_load_dir(VeloFileDialog *dlg) {
    dlg->entry_count = 0;
    if (strcmp(dlg->current_path, "/") != 0 && strcmp(dlg->current_path, "C:/") != 0 && strcmp(dlg->current_path, "C:") != 0) {
        strcpy(dlg->entries[0].name, "..");
        dlg->entries[0].is_dir = 1;
        dlg->entries[0].size = 0;
        dlg->entry_count = 1;
    }

    int loaded = velo_list_dir(dlg->current_path, &dlg->entries[dlg->entry_count], GFD_MAX_ENTRIES - dlg->entry_count);
    if (loaded > 0) dlg->entry_count += loaded;
    dlg->selected_idx = -1;
    dlg->scroll_offset = 0;
}

static inline void velo_gfd_init(VeloFileDialog *dlg, int mode, const char *initial_path) {
    dlg->mode = mode;
    dlg->filter_type = 0;
    dlg->show_extensions = 0;
    if (initial_path && initial_path[0]) {
        snprintf(dlg->current_path, sizeof(dlg->current_path), "%s", initial_path);
    } else {
        strcpy(dlg->current_path, "C:/Users/Desktop");
    }
    
    dlg->filename_input[0] = '\0';
    dlg->cursor_pos = 0;
    dlg->mkdir_active = 0;
    velo_gfd_load_dir(dlg);
}

static inline void velo_gfd_nav_up(VeloFileDialog *dlg) {
    if (strcmp(dlg->current_path, "/") == 0 || strcmp(dlg->current_path, "C:/") == 0 || strcmp(dlg->current_path, "C:") == 0) return;
    char *s = strrchr(dlg->current_path, '/');
    if (!s) s = strrchr(dlg->current_path, '\\');
    if (s && s != dlg->current_path) {
        if (s - dlg->current_path == 2 && dlg->current_path[1] == ':') {
            *(s + 1) = '\0';
        } else {
            *s = '\0';
        }
    } else {
        strcpy(dlg->current_path, "C:/");
    }
    velo_gfd_load_dir(dlg);
}

static inline void velo_gfd_get_selected_path(VeloFileDialog *dlg, char *out_path, size_t max_len) {
    if (!dlg || !out_path || max_len == 0) return;

    char final_name[64];
    strcpy(final_name, dlg->filename_input);

    if (dlg->filter_type == 0 && strstr(final_name, ".") == NULL) {
        strcat(final_name, ".txt");
    }

    size_t pl = strlen(dlg->current_path);
    if (pl > 0 && (dlg->current_path[pl - 1] == '/' || dlg->current_path[pl - 1] == '\\')) {
        snprintf(out_path, max_len, "%s%s", dlg->current_path, final_name);
    } else {
        snprintf(out_path, max_len, "%s/%s", dlg->current_path, final_name);
    }
}

static inline void velo_gfd_render(velo_window_t win, VeloFileDialog *dlg, int w, int h) {
    int dlg_w = 560;
    int dlg_h = 370;
    int dlg_x = (w - dlg_w) / 2;
    int dlg_y = (h - dlg_h) / 2;

    velo_window_draw_rect_color(win, dlg_x - 4, dlg_y - 4, dlg_w + 8, dlg_h + 8, 0x000F172A);
    velo_window_draw_gradient(win, dlg_x, dlg_y, dlg_w, dlg_h, 0x001E293B, 0x000F172A);
    velo_window_draw_rect_color(win, dlg_x, dlg_y, dlg_w, 1, 0x0038BDF8);
    velo_window_draw_rect_color(win, dlg_x, dlg_y + dlg_h - 1, dlg_w, 1, 0x00334155);

    velo_window_draw_gradient(win, dlg_x, dlg_y, dlg_w, 30, 0x000284C7, 0x000369A1);
    velo_window_draw_text_colored(win, (dlg->mode == GFD_MODE_OPEN) ? "Datei oeffnen" : "Speichern unter", dlg_x + 14, dlg_y + 7, 0x00FFFFFF);

    velo_ui_draw_button(win, dlg_x + 14, dlg_y + 36, 28, 24, "<", 0, 0, 0, 0);
    velo_ui_draw_button(win, dlg_x + dlg_w - 120, dlg_y + 36, 106, 24, "+ Neuer Ordner", 0, 0, 0, 0);

    int path_x = dlg_x + 48;
    int path_w = dlg_w - 174;
    velo_window_draw_gradient(win, path_x, dlg_y + 36, path_w, 24, 0x000F172A, 0x001E293B);
    velo_window_draw_rect_color(win, path_x, dlg_y + 36, path_w, 1, 0x00334155);
    velo_window_draw_rect_color(win, path_x, dlg_y + 59, path_w, 1, 0x00334155);
    velo_window_draw_rect_color(win, path_x, dlg_y + 36, 1, 24, 0x00334155);
    velo_window_draw_rect_color(win, path_x + path_w - 1, dlg_y + 36, 1, 24, 0x00334155);
    velo_window_draw_text_colored(win, dlg->current_path, path_x + 8, dlg_y + 40, 0x0038BDF8);

    int list_x = dlg_x + 14;
    int list_y = dlg_y + 66;
    int list_w = dlg_w - 28;
    int list_h = 210;

    velo_window_draw_rect_color(win, list_x, list_y, list_w, list_h, 0x000F172A);
    velo_window_draw_rect_color(win, list_x, list_y, list_w, 1, 0x00334155);
    velo_window_draw_rect_color(win, list_x, list_y + list_h - 1, list_w, 1, 0x00334155);
    velo_window_draw_rect_color(win, list_x, list_y, 1, list_h, 0x00334155);
    velo_window_draw_rect_color(win, list_x + list_w - 1, list_y, 1, list_h, 0x00334155);

    int max_visible = 9;
    for (int i = 0; i < max_visible && (i + dlg->scroll_offset < dlg->entry_count); i++) {
        int idx = i + dlg->scroll_offset;
        int item_y = list_y + 4 + i * 22;

        if (idx == dlg->selected_idx) {
            velo_window_draw_gradient(win, list_x + 2, item_y - 2, list_w - 20, 20, 0x000284C7, 0x000369A1);
            velo_window_draw_rect_color(win, list_x + 2, item_y - 2, list_w - 20, 1, 0x0038BDF8);
        }

        if (dlg->entries[idx].is_dir) {
            velo_window_draw_icon(win, VELO_ICON_FOLDER, list_x + 6, item_y, 16);
        } else {
            velo_window_draw_icon(win, VELO_ICON_DOC, list_x + 6, item_y, 16);
        }

        char display_name[64];
        strcpy(display_name, dlg->entries[idx].name);
        if (!dlg->show_extensions && !dlg->entries[idx].is_dir) {
            char *dot = strrchr(display_name, '.');
            if (dot) *dot = '\0';
        }

        UINT32 item_fg = (idx == dlg->selected_idx) ? 0x00FFFFFF : velo_get_contrast_color(0x000F172A);
        velo_window_draw_text_colored(win, display_name, list_x + 28, item_y + 2, item_fg);
    }

    velo_window_draw_text_colored(win, "Dateiname:", dlg_x + 14, dlg_y + 288, 0x0094A3B8);
    int in_x = dlg_x + 100, in_y = dlg_y + 284, in_w = dlg_w - 250;
    velo_window_draw_gradient(win, in_x, in_y, in_w, 24, 0x000F172A, 0x001E293B);
    velo_window_draw_rect_color(win, in_x, in_y, in_w, 1, 0x0038BDF8);
    velo_window_draw_rect_color(win, in_x, in_y + 23, in_w, 1, 0x0038BDF8);
    velo_window_draw_rect_color(win, in_x, in_y, 1, 24, 0x0038BDF8);
    velo_window_draw_rect_color(win, in_x + in_w - 1, in_y, 1, 24, 0x0038BDF8);
    velo_window_draw_text_colored(win, dlg->filename_input, in_x + 6, in_y + 4, 0x00FFFFFF);
    velo_window_draw_rect_color(win, in_x + 6 + dlg->cursor_pos * 8, in_y + 4, 1, 16, 0x0038BDF8);

    int dd_x = in_x + in_w + 6, dd_w = 130;
    velo_window_draw_gradient(win, dd_x, in_y, dd_w, 24, 0x001E293B, 0x000F172A);
    velo_window_draw_rect_color(win, dd_x, in_y, dd_w, 1, 0x00334155);
    velo_window_draw_rect_color(win, dd_x, in_y + 23, dd_w, 1, 0x00334155);
    velo_window_draw_rect_color(win, dd_x, in_y, 1, 24, 0x00334155);
    velo_window_draw_rect_color(win, dd_x + dd_w - 1, in_y, 1, 24, 0x00334155);
    velo_window_draw_text_colored(win, (dlg->filter_type == 0) ? "*.txt" : "*.*", dd_x + 8, in_y + 4, 0x00FFFFFF);
    velo_window_draw_text_colored(win, "v", dd_x + dd_w - 16, in_y + 4, 0x0064748B);

    int ok_x = dlg_x + dlg_w - 190;
    int can_x = dlg_x + dlg_w - 95;
    int btn_y = dlg_y + dlg_h - 44;
    velo_ui_draw_button(win, ok_x, btn_y, 85, 28, (dlg->mode == GFD_MODE_OPEN) ? "Oeffnen" : "Speichern", 0, 0, 0, 0);
    velo_ui_draw_button(win, can_x, btn_y, 80, 28, "Abbrechen", 0, 0, 0, 0);

    if (dlg->mkdir_active) {
        int m_w = 340, m_h = 130;
        int m_x = dlg_x + (dlg_w - m_w) / 2;
        int m_y = dlg_y + (dlg_h - m_h) / 2;

        velo_window_draw_rect_color(win, m_x - 3, m_y - 3, m_w + 6, m_h + 6, 0x00020617);
        velo_window_draw_gradient(win, m_x, m_y, m_w, m_h, 0x001E293B, 0x000F172A);
        velo_window_draw_rect_color(win, m_x, m_y, m_w, 1, 0x0038BDF8);
        velo_window_draw_text_colored(win, "Neuen Ordner erstellen:", m_x + 14, m_y + 12, 0x00FFFFFF);

        int min_x = m_x + 14, min_y = m_y + 36, min_w = m_w - 28;
        velo_window_draw_gradient(win, min_x, min_y, min_w, 24, 0x000F172A, 0x001E293B);
        velo_window_draw_rect_color(win, min_x, min_y, min_w, 1, 0x0038BDF8);
        velo_window_draw_rect_color(win, min_x, min_y + 23, min_w, 1, 0x0038BDF8);
        velo_window_draw_rect_color(win, min_x, min_y, 1, 24, 0x0038BDF8);
        velo_window_draw_rect_color(win, min_x + min_w - 1, min_y, 1, 24, 0x0038BDF8);
        velo_window_draw_text_colored(win, dlg->mkdir_input, min_x + 6, min_y + 4, 0x00FFFFFF);

        velo_ui_draw_button(win, m_x + m_w - 150, m_y + m_h - 36, 65, 24, "OK", 0, 0, 0, 0);
        velo_ui_draw_button(win, m_x + m_w - 75, m_y + m_h - 36, 65, 24, "Abbrechen", 0, 0, 0, 0);
    }
}

static inline int velo_gfd_handle_click(VeloFileDialog *dlg, int w, int h, int click_x, int click_y) {
    int dlg_w = 560, dlg_h = 370;
    int dlg_x = (w - dlg_w) / 2, dlg_y = (h - dlg_h) / 2;

    if (dlg->mkdir_active) {
        int m_w = 340, m_h = 130;
        int m_x = dlg_x + (dlg_w - m_w) / 2;
        int m_y = dlg_y + (dlg_h - m_h) / 2;

        if (velo_ui_in_rect(click_x, click_y, m_x + m_w - 150, m_y + m_h - 36, 65, 24)) {
            if (dlg->mkdir_input[0]) {
                char full[256];
                size_t pl = strlen(dlg->current_path);
                if (pl > 0 && (dlg->current_path[pl-1] == '/' || dlg->current_path[pl-1] == '\\'))
                    snprintf(full, sizeof(full), "%s%s", dlg->current_path, dlg->mkdir_input);
                else
                    snprintf(full, sizeof(full), "%s/%s", dlg->current_path, dlg->mkdir_input);
                velo_mkdir(full);
                velo_gfd_load_dir(dlg);
            }
            dlg->mkdir_active = 0;
            return GFD_ACTION_NONE;
        }
        if (velo_ui_in_rect(click_x, click_y, m_x + m_w - 75, m_y + m_h - 36, 65, 24)) {
            dlg->mkdir_active = 0;
            return GFD_ACTION_NONE;
        }
        return GFD_ACTION_NONE;
    }

    if (velo_ui_in_rect(click_x, click_y, dlg_x + 14, dlg_y + 36, 28, 24)) {
        velo_gfd_nav_up(dlg);
        return GFD_ACTION_NONE;
    }

    if (velo_ui_in_rect(click_x, click_y, dlg_x + dlg_w - 120, dlg_y + 36, 106, 24)) {
        dlg->mkdir_active = 1;
        strcpy(dlg->mkdir_input, "NeuerOrdner");
        dlg->mkdir_cursor = (int)strlen(dlg->mkdir_input);
        return GFD_ACTION_NONE;
    }

    int in_x = dlg_x + 100, in_w = dlg_w - 250;
    int dd_x = in_x + in_w + 6, dd_w = 130;
    if (velo_ui_in_rect(click_x, click_y, dd_x, dlg_y + 284, dd_w, 24)) {
        dlg->filter_type = !dlg->filter_type;
        return GFD_ACTION_NONE;
    }

    int ok_x = dlg_x + dlg_w - 190, can_x = dlg_x + dlg_w - 95, btn_y = dlg_y + dlg_h - 44;
    if (velo_ui_in_rect(click_x, click_y, ok_x, btn_y, 85, 28)) {
        if (dlg->filename_input[0]) return GFD_ACTION_CONFIRM;
    }
    if (velo_ui_in_rect(click_x, click_y, can_x, btn_y, 80, 28)) {
        return GFD_ACTION_CANCEL;
    }

    int list_x = dlg_x + 14, list_y = dlg_y + 66, list_w = dlg_w - 28;
    if (velo_ui_in_rect(click_x, click_y, list_x, list_y, list_w, 210)) {
        int clicked_row = (click_y - (list_y + 4)) / 22;
        int idx = clicked_row + dlg->scroll_offset;
        if (idx >= 0 && idx < dlg->entry_count) {
            if (dlg->entries[idx].is_dir) {
                if (strcmp(dlg->entries[idx].name, "..") == 0) {
                    velo_gfd_nav_up(dlg);
                } else {
                    char next_p[256];
                    size_t pl = strlen(dlg->current_path);
                    if (pl > 0 && (dlg->current_path[pl-1] == '/' || dlg->current_path[pl-1] == '\\'))
                        snprintf(next_p, sizeof(next_p), "%s%s", dlg->current_path, dlg->entries[idx].name);
                    else
                        snprintf(next_p, sizeof(next_p), "%s/%s", dlg->current_path, dlg->entries[idx].name);
                    strcpy(dlg->current_path, next_p);
                    velo_gfd_load_dir(dlg);
                }
            } else {
                dlg->selected_idx = idx;
                strcpy(dlg->filename_input, dlg->entries[idx].name);
                dlg->cursor_pos = (int)strlen(dlg->filename_input);
            }
        }
    }

    return GFD_ACTION_NONE;
}

static inline int velo_gfd_handle_key(VeloFileDialog *dlg, char key) {
    if (dlg->mkdir_active) {
        if (key == '\n') {
            if (dlg->mkdir_input[0]) {
                char full[256];
                size_t pl = strlen(dlg->current_path);
                if (pl > 0 && (dlg->current_path[pl-1] == '/' || dlg->current_path[pl-1] == '\\'))
                    snprintf(full, sizeof(full), "%s%s", dlg->current_path, dlg->mkdir_input);
                else
                    snprintf(full, sizeof(full), "%s/%s", dlg->current_path, dlg->mkdir_input);
                velo_mkdir(full);
                velo_gfd_load_dir(dlg);
            }
            dlg->mkdir_active = 0;
        } else if (key == (char)0x1B) {
            dlg->mkdir_active = 0;
        } else if (key == '\b') {
            int l = (int)strlen(dlg->mkdir_input);
            if (dlg->mkdir_cursor > 0) {
                for (int i = dlg->mkdir_cursor - 1; i < l; i++) dlg->mkdir_input[i] = dlg->mkdir_input[i + 1];
                dlg->mkdir_cursor--;
            }
        } else if ((unsigned char)key >= 32 && (unsigned char)key < 127 && strlen(dlg->mkdir_input) < 28) {
            int l = (int)strlen(dlg->mkdir_input);
            for (int i = l; i >= dlg->mkdir_cursor; i--) dlg->mkdir_input[i + 1] = dlg->mkdir_input[i];
            dlg->mkdir_input[dlg->mkdir_cursor] = key;
            dlg->mkdir_cursor++;
        }
        return GFD_ACTION_NONE;
    }

    if (key == '\n') {
        if (dlg->filename_input[0]) return GFD_ACTION_CONFIRM;
    } else if (key == (char)0x1B) {
        return GFD_ACTION_CANCEL;
    } else if (key == '\b') {
        int l = (int)strlen(dlg->filename_input);
        if (dlg->cursor_pos > 0) {
            for (int i = dlg->cursor_pos - 1; i < l; i++) dlg->filename_input[i] = dlg->filename_input[i + 1];
            dlg->cursor_pos--;
        }
    } else if (key == (char)0x84 || key == KEY_LEFT) {
        if (dlg->cursor_pos > 0) dlg->cursor_pos--;
    } else if (key == (char)0x85 || key == KEY_RIGHT) {
        int l = (int)strlen(dlg->filename_input);
        if (dlg->cursor_pos < l) dlg->cursor_pos++;
    } else if ((unsigned char)key >= 32 && (unsigned char)key < 127 && strlen(dlg->filename_input) < 60) {
        int l = (int)strlen(dlg->filename_input);
        for (int i = l; i >= dlg->cursor_pos; i--) dlg->filename_input[i + 1] = dlg->filename_input[i];
        dlg->filename_input[dlg->cursor_pos] = key;
        dlg->cursor_pos++;
    }
    return GFD_ACTION_NONE;
}

#ifdef __cplusplus
}
#endif

#endif /* _VELO_WINDOW_H */
#ifndef _VELO_WINDOW_H
#define _VELO_WINDOW_H

#include <velo/syscall.h>
#include <string.h>

typedef int velo_window_t;

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
    return (px >= x && px <= x + w && py >= y && py <= y + h);
}

// ====================================================
// WINDOWS TEXT-CURSOR BERECHNUNG (KLICK AN MAUSPOSITION)
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

// ====================================================
// WINDOWS TEXT-NAVIGATION (PFEILTASTEN RAUF / RUNTER)
// ====================================================
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
// DELEGIERTE UI WIDGET SYSCALLS
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

    velo_window_draw_text_colored(win, prompt, x + 20, y + 44, 0x000F172A);

    int in_x = x + 20, in_y = y + 68, in_w = w - 40;
    velo_window_draw_gradient(win, in_x, in_y, in_w, 28, 0x00FFFFFF, 0x00F8FAFC);
    velo_window_draw_rect_color(win, in_x, in_y, in_w, 1, 0x000284C7);
    velo_window_draw_rect_color(win, in_x, in_y + 27, in_w, 1, 0x000284C7);
    velo_window_draw_rect_color(win, in_x, in_y, 1, 28, 0x000284C7);
    velo_window_draw_rect_color(win, in_x + in_w - 1, in_y, 1, 28, 0x000284C7);

    velo_window_draw_text_colored(win, text_val, in_x + 8, in_y + 6, 0x000F172A);
    velo_window_draw_rect_color(win, in_x + 8 + cursor_pos * 8, in_y + 5, 1, 16, 0x000284C7);

    int ok_x = x + w - 170, ok_y = y + h - 40;
    velo_ui_draw_button(win, ok_x, ok_y, 70, 26, btn_ok, 0, 0, 0, 0);

    int can_x = x + w - 90;
    velo_ui_draw_button(win, can_x, ok_y, 70, 26, btn_cancel, 0, 0, 0, 0);
}

#endif
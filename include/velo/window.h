#ifndef _VELO_WINDOW_H
#define _VELO_WINDOW_H

#include <velo/syscall.h>
#include <string.h>

typedef int velo_window_t;

// ==========================================
// BASIS-FENSTER SYSCALLS
// ==========================================
static inline velo_window_t velo_window_create(const char *title, int width, int height) {
    return (velo_window_t)velo_syscall(SYS_CREATE_WINDOW, (UINT64)title, (UINT64)width, (UINT64)height, 0);
}

static inline void velo_window_clear(velo_window_t win) {
    velo_syscall(SYS_CLEAR_WINDOW, (UINT64)win, 0, 0, 0);
}

static inline void velo_window_draw_rect(velo_window_t win, int x, int y, int w, int h) {
    UINT64 packed = ((UINT64)w << 32) | (UINT32)h;
    velo_syscall(SYS_DRAW_RECT, (UINT64)win, (UINT64)x, (UINT64)y, packed);
}

static inline void velo_window_draw_rect_color(velo_window_t win, int x, int y, int w, int h, UINT32 color) {
    UINT64 packed_dim = ((UINT64)w << 32) | (UINT32)h;
    velo_syscall(SYS_DRAW_RECT_COL, (UINT64)win, (UINT64)x, (UINT64)y, packed_dim);
    (void)color;
}

static inline void velo_window_draw_gradient(velo_window_t win, int x, int y, int w, int h, UINT32 top_col, UINT32 bot_col) {
    UINT64 packed_dim = ((UINT64)w << 32) | (UINT32)h;
    UINT64 packed_col = ((UINT64)top_col << 32) | (UINT32)bot_col;
    velo_syscall(SYS_DRAW_GRADIENT, (UINT64)win, (UINT64)x, (UINT64)y, packed_dim);
    (void)packed_col;
}

static inline void velo_window_draw_text(velo_window_t win, const char *text, int x, int y) {
    velo_syscall(SYS_DRAW_TEXT, (UINT64)win, (UINT64)text, (UINT64)x, (UINT64)y);
}

static inline void velo_window_draw_text_colored(velo_window_t win, const char *text, int x, int y, UINT32 fg_col) {
    velo_syscall(SYS_DRAW_TEXT_COL, (UINT64)win, (UINT64)text, (UINT64)x, ((UINT64)y << 32) | (UINT32)fg_col);
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

// ==========================================
// HIGH-LEVEL VISTA WIDGET FRAMEWORK
// ==========================================

/* 1. Vista Aero Glass Button */
static inline void velo_ui_button(velo_window_t win, int x, int y, int w, int h, const char *label, int is_hover) {
    if (is_hover) {
        velo_window_draw_gradient(win, x, y, w, h, 0x00EBF4FB, 0x00CDE5FE);
        velo_window_draw_rect_color(win, x, y, w, 1, 0x0078B4E6);
        velo_window_draw_rect_color(win, x, y + h - 1, w, 1, 0x005A96D2);
    } else {
        velo_window_draw_gradient(win, x, y, w, h, 0x00F8FAFC, 0x00E2E8F0);
        velo_window_draw_rect_color(win, x, y, w, 1, 0x00CBD5E1);
        velo_window_draw_rect_color(win, x, y + h - 1, w, 1, 0x0094A3B8);
    }
    velo_window_draw_text_colored(win, label, x + (w - (int)strlen(label) * 8) / 2, y + (h - 16) / 2 + 1, 0x000F172A);
}

/* 2. Breadcrumb Adressleiste */
static inline void velo_ui_addressbar(velo_window_t win, int x, int y, int w, const char *path) {
    velo_window_draw_gradient(win, x, y, 22, 24, 0x0060A5FA, 0x002563EB);
    velo_window_draw_text_colored(win, "<", x + 7, y + 4, 0x00FFFFFF);

    velo_window_draw_gradient(win, x + 24, y, 22, 24, 0x00CBD5E1, 0x0094A3B8);
    velo_window_draw_text_colored(win, ">", x + 31, y + 4, 0x00FFFFFF);

    int bar_x = x + 50;
    int bar_w = w - 50;
    velo_window_draw_gradient(win, bar_x, y, bar_w, 24, 0x00FFFFFF, 0x00F1F5F9);
    velo_window_draw_rect_color(win, bar_x, y, bar_w, 1, 0x0094A3B8);
    velo_window_draw_rect_color(win, bar_x, y + 23, bar_w, 1, 0x00CBD5E1);
    velo_window_draw_text_colored(win, path, bar_x + 8, y + 4, 0x001E293B);
}

/* 3. Suchfeld */
static inline void velo_ui_searchbox(velo_window_t win, int x, int y, int w, const char *query, int focused) {
    UINT32 border = focused ? 0x003B82F6 : 0x0094A3B8;
    velo_window_draw_gradient(win, x, y, w, 24, 0x00FFFFFF, 0x00F8FAFC);
    velo_window_draw_rect_color(win, x, y, w, 1, border);
    velo_window_draw_rect_color(win, x, y + 23, w, 1, border);

    if (query && query[0]) {
        velo_window_draw_text_colored(win, query, x + 8, y + 4, 0x000F172A);
        if (focused) {
            velo_window_draw_text_colored(win, "|", x + 8 + (int)strlen(query) * 8, y + 4, 0x003B82F6);
        }
    } else {
        velo_window_draw_text_colored(win, "* Suchen...", x + 8, y + 4, 0x0094A3B8);
    }
}

/* 4. Toolbar */
static inline void velo_ui_toolbar(velo_window_t win, int y, int w, const char *actions) {
    velo_window_draw_gradient(win, 0, y, w, 22, 0x00EFF6FF, 0x00DBEAFE);
    velo_window_draw_rect_color(win, 0, y + 21, w, 1, 0x00BFDBFE);
    velo_window_draw_text_colored(win, actions, 10, y + 3, 0x001E3A8A);
}

/* 5. Sidebar */
static inline void velo_ui_sidebar_panel(velo_window_t win, int y, int w, int h) {
    velo_window_draw_gradient(win, 0, y, w, h, 0x00F8FAFC, 0x00F1F5F9);
    velo_window_draw_rect_color(win, w - 1, y, 1, h, 0x00CBD5E1);
}

/* 6. Sidebar Item */
static inline void velo_ui_sidebar_item(velo_window_t win, int y, int w, const char *label, int is_selected) {
    if (is_selected) {
        velo_window_draw_gradient(win, 4, y - 2, w - 8, 20, 0x00EBF4FB, 0x00CDE5FE);
        velo_window_draw_rect_color(win, 4, y - 2, w - 8, 1, 0x0078B4E6);
        velo_window_draw_text_colored(win, label, 12, y, 0x000F172A);
    } else {
        velo_window_draw_text_colored(win, label, 12, y, 0x00334155);
    }
}

/* 7. Tabellen-Header */
static inline void velo_ui_table_header(velo_window_t win, int x, int y, int w) {
    velo_window_draw_gradient(win, x, y, w, 20, 0x00F8FAFC, 0x00E2E8F0);
    velo_window_draw_rect_color(win, x, y + 19, w, 1, 0x00CBD5E1);
}

/* 8. Tabellen-Zeile */
static inline void velo_ui_file_row(velo_window_t win, int x, int y, int w, const char *icon, const char *name, const char *date, const char *type, const char *size, int is_selected) {
    if (is_selected) {
        velo_window_draw_gradient(win, x + 2, y - 3, w - 4, 22, 0x00EBF4FB, 0x00D6ECFF);
        velo_window_draw_rect_color(win, x + 2, y - 3, w - 4, 1, 0x0060A5FA);
        velo_window_draw_rect_color(win, x + 2, y + 18, w - 4, 1, 0x0060A5FA);
    }
    velo_window_draw_text_colored(win, icon, x + 4, y, is_selected ? 0x000F172A : 0x002563EB);
    velo_window_draw_text_colored(win, name, x + 44, y, 0x000F172A);
    velo_window_draw_text_colored(win, date, x + 180, y, 0x0064748B);
    velo_window_draw_text_colored(win, type, x + 310, y, 0x00334155);
    velo_window_draw_text_colored(win, size, x + 390, y, 0x0064748B);
}

/* 9. Details Pane */
static inline void velo_ui_details_pane(velo_window_t win, int y, int w, int h, const char *icon, const char *title, const char *subtitle, const char *btn_text) {
    velo_window_draw_gradient(win, 0, y, w, h, 0x00E2E8F0, 0x00CBD5E1);
    velo_window_draw_rect_color(win, 0, y, w, 1, 0x0094A3B8);

    if (title && title[0]) {
        velo_window_draw_text_colored(win, icon ? icon : "[DOC]", 14, y + 12, 0x002563EB);
        velo_window_draw_text_colored(win, title, 62, y + 6, 0x000F172A);
        velo_window_draw_text_colored(win, subtitle, 62, y + 22, 0x00475569);

        if (btn_text) {
            velo_ui_button(win, w - 140, y + 8, 130, 28, btn_text, 1);
        }
    }
}

#endif
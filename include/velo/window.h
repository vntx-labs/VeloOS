#ifndef _VELO_WINDOW_H
#define _VELO_WINDOW_H

#include <velo/syscall.h>
#include <string.h>

typedef int velo_window_t;

#define VELO_EV_NONE    0
#define VELO_EV_CLICK   1
#define VELO_EV_KEY     2
#define VELO_EV_RESIZE  3

// ==========================================
// STANDARD-LIBRARY FENSTER-FUNKTIONEN
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

// ==========================================
// VISTA AERO EXPLORER WIDGETS
// ==========================================

/* 1. Vista Runder Glass Navigations-Button (< und >) */
static inline void velo_ui_nav_btn(velo_window_t win, int x, int y, const char *symbol, int enabled, int is_hover) {
    if (!enabled) {
        velo_window_draw_gradient(win, x, y, 26, 26, 0x00E2E8F0, 0x00CBD5E1);
        velo_window_draw_rect_color(win, x, y, 26, 1, 0x00CBD5E1);
        velo_window_draw_text_colored(win, symbol, x + 9, y + 5, 0x0094A3B8);
        return;
    }
    if (is_hover) {
        velo_window_draw_gradient(win, x, y, 26, 26, 0x0038BDF8, 0x000369A1);
        velo_window_draw_rect_color(win, x, y, 26, 1, 0x00BAE6FD);
    } else {
        velo_window_draw_gradient(win, x, y, 26, 26, 0x000284C7, 0x000F4866);
        velo_window_draw_rect_color(win, x, y, 26, 1, 0x0038BDF8);
    }
    velo_window_draw_text_colored(win, symbol, x + 9, y + 5, 0x00FFFFFF);
}

/* 2. Vista Breadcrumb-Adressleiste */
static inline void velo_ui_addressbar(velo_window_t win, int x, int y, int w, const char *path) {
    velo_window_draw_gradient(win, x, y, w, 26, 0x00FFFFFF, 0x00F8FAFC);
    velo_window_draw_rect_color(win, x, y, w, 1, 0x007BA3B8);
    velo_window_draw_rect_color(win, x, y + 25, w, 1, 0x0094A3B8);
    velo_window_draw_rect_color(win, x, y, 1, 26, 0x007BA3B8);
    velo_window_draw_rect_color(win, x + w - 1, y, 1, 26, 0x007BA3B8);

    velo_window_draw_text_colored(win, "[=]", x + 8, y + 5, 0x000284C7);
    velo_window_draw_text_colored(win, path, x + 38, y + 5, 0x000F172A);

    velo_window_draw_text_colored(win, "v", x + w - 38, y + 5, 0x0064748B);
    velo_window_draw_rect_color(win, x + w - 26, y + 4, 1, 18, 0x00CBD5E1);
    velo_window_draw_text_colored(win, ">", x + w - 16, y + 5, 0x000284C7);
}

/* 3. Vista Suchfeld */
static inline void velo_ui_searchbox(velo_window_t win, int x, int y, int w, const char *query, int focused) {
    UINT32 border = focused ? 0x000284C7 : 0x007BA3B8;
    velo_window_draw_gradient(win, x, y, w, 26, 0x00FFFFFF, 0x00F8FAFC);
    velo_window_draw_rect_color(win, x, y, w, 1, border);
    velo_window_draw_rect_color(win, x, y + 25, w, 1, border);
    velo_window_draw_rect_color(win, x, y, 1, 24, border);
    velo_window_draw_rect_color(win, x + w - 1, y, 1, 24, border);

    if (query && query[0]) {
        velo_window_draw_text_colored(win, query, x + 8, y + 4, 0x000F172A);
        if (focused) {
            velo_window_draw_text_colored(win, "|", x + 8 + (int)strlen(query) * 8, y + 4, 0x000284C7);
        }
    } else {
        velo_window_draw_text_colored(win, "Search", x + 8, y + 4, 0x0094A3B8);
    }
    velo_window_draw_text_colored(win, "*", x + w - 18, y + 4, 0x000284C7);
}

/* 4. Vista Aero Glass Command Bar */
static inline void velo_ui_command_bar(velo_window_t win, int y, int w) {
    velo_window_draw_gradient(win, 0, y, w, 28, 0x001B4D68, 0x000A2434);
    velo_window_draw_rect_color(win, 0, y, w, 1, 0x003A7088);
    velo_window_draw_rect_color(win, 0, y + 27, w, 1, 0x0005141C);

    velo_window_draw_text_colored(win, "[=] Organize v", 10, y + 6, 0x00FFFFFF);
    velo_window_draw_rect_color(win, 130, y + 4, 1, 20, 0x002B5268);

    velo_window_draw_text_colored(win, "[#] Views v", 140, y + 6, 0x00FFFFFF);
    velo_window_draw_rect_color(win, 235, y + 4, 1, 20, 0x002B5268);

    velo_window_draw_text_colored(win, "[*] System properties", 245, y + 6, 0x00FFFFFF);
    velo_window_draw_rect_color(win, 430, y + 4, 1, 20, 0x002B5268);

    velo_window_draw_text_colored(win, "Uninstall or change a program", 440, y + 6, 0x00FFFFFF);
    velo_window_draw_rect_color(win, 695, y + 4, 1, 20, 0x002B5268);

    velo_window_draw_text_colored(win, "Map network drive", 705, y + 6, 0x00FFFFFF);
    velo_window_draw_text_colored(win, ">>", w - 46, y + 6, 0x00FFFFFF);
    velo_window_draw_text_colored(win, "(?)", w - 24, y + 6, 0x005AC0E0);
}

/* 5. Vista Sidebar Item */
static inline void velo_ui_sidebar_item(velo_window_t win, int y, int w, const char *label, int is_selected) {
    if (is_selected) {
        velo_window_draw_gradient(win, 6, y - 3, w - 12, 22, 0x00EBF4FB, 0x00D6ECFF);
        velo_window_draw_rect_color(win, 6, y - 3, w - 12, 1, 0x0060A5FA);
        velo_window_draw_rect_color(win, 6, y + 18, w - 12, 1, 0x0060A5FA);
        velo_window_draw_rect_color(win, 6, y - 3, 1, 22, 0x0060A5FA);
        velo_window_draw_rect_color(win, 6 + w - 13, y - 3, 1, 22, 0x0060A5FA);
        velo_window_draw_text_colored(win, label, 14, y, 0x000F172A);
    } else {
        velo_window_draw_text_colored(win, label, 14, y, 0x001D4ED8);
    }
}

/* 6. Vista Aqua-Glass Drive Speicherbalken */
static inline void velo_ui_vista_storage_bar(velo_window_t win, int x, int y, int w, int percent) {
    velo_window_draw_gradient(win, x, y, w, 14, 0x00E2E8F0, 0x00CBD5E1);
    velo_window_draw_rect_color(win, x, y, w, 1, 0x007BA3B8);
    velo_window_draw_rect_color(win, x, y + 13, w, 1, 0x007BA3B8);
    velo_window_draw_rect_color(win, x, y, 1, 14, 0x007BA3B8);
    velo_window_draw_rect_color(win, x + w - 1, y, 1, 14, 0x007BA3B8);

    int fill = ((w - 2) * percent) / 100;
    if (fill > 0) {
        velo_window_draw_gradient(win, x + 1, y + 1, fill, 6, 0x007DD3FC, 0x000284C7);
        velo_window_draw_gradient(win, x + 1, y + 7, fill, 6, 0x000369A1, 0x0038BDF8);
        velo_window_draw_rect_color(win, x + 1, y + 6, fill, 1, 0x00BAE6FD);
    }
}

#endif
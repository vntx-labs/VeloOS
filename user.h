#ifndef USER_H
#define USER_H

#include <velo/syscall.h>

static inline void velo_exit(void) {
    velo_syscall(SYS_EXIT, 0, 0, 0, 0);
}

static inline int velo_create_window(const char *title, int w, int h) {
    return (int)velo_syscall(SYS_CREATE_WINDOW, (UINT64)title, (UINT64)w, (UINT64)h, 0);
}

static inline void velo_clear_window(int win_id) {
    velo_syscall(SYS_CLEAR_WINDOW, (UINT64)win_id, 0, 0, 0);
}

static inline void velo_draw_rect(int win_id, int x, int y, int w, int h) {
    UINT64 packed = ((UINT64)w << 32) | (UINT32)h;
    velo_syscall(SYS_DRAW_RECT, (UINT64)win_id, (UINT64)x, (UINT64)y, packed);
}

static inline void velo_draw_rect_col(int win_id, int x, int y, int w, int h, UINT32 col) {
    UINT64 p1 = ((UINT64)x << 32) | (UINT32)y;
    UINT64 p2 = ((UINT64)w << 32) | (UINT32)h;
    velo_syscall(SYS_DRAW_RECT_COL, (UINT64)win_id, p1, p2, (UINT64)col);
}

static inline void velo_draw_gradient(int win_id, int x, int y, int w, int h, UINT32 top_col, UINT32 bot_col) {
    UINT64 p1 = ((UINT64)x << 32) | (UINT32)y;
    UINT64 p2 = ((UINT64)w << 32) | (UINT32)h;
    UINT64 cols = ((UINT64)top_col << 32) | (UINT32)bot_col;
    velo_syscall(SYS_DRAW_GRADIENT, (UINT64)win_id, p1, p2, cols);
}

static inline void velo_draw_text(int win_id, const char *text, int x, int y) {
    velo_syscall(SYS_DRAW_TEXT, (UINT64)win_id, (UINT64)text, (UINT64)x, (UINT64)y);
}

static inline void velo_draw_text_col(int win_id, const char *text, int x, int y, UINT32 col) {
    UINT64 p_col = ((UINT64)y << 32) | (UINT32)col;
    velo_syscall(SYS_DRAW_TEXT_COL, (UINT64)win_id, (UINT64)text, (UINT64)x, p_col);
}

static inline int velo_get_event(int win_id, UserEvent *ev) {
    return (int)velo_syscall(SYS_GET_EVENT, (UINT64)win_id, (UINT64)ev, 0, 0);
}

static inline void velo_mark_dirty(void) {
    velo_syscall(SYS_MARK_DIRTY, 0, 0, 0, 0);
}

static inline void* velo_heap_alloc(UINT64 bytes) {
    return (void*)(UINTN)velo_syscall(SYS_HEAP_ALLOC, bytes, 0, 0, 0);
}

static inline int velo_flush_gui_map(int win_id, VeloDrawCommand *cmds, int count) {
    return (int)velo_syscall(SYS_FLUSH_GUI_MAP, (UINT64)win_id, (UINT64)cmds, (UINT64)count, 0);
}

static inline void velo_sleep(UINT32 ticks) {
    velo_syscall(SYS_TASK_SLEEP, (UINT64)ticks, 0, 0, 0);
}

static inline void velo_yield(void) {
    velo_syscall(SYS_TASK_YIELD, 0, 0, 0, 0);
}

#endif /* USER_H */
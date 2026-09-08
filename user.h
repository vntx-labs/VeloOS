#ifndef USER_H
#define USER_H

typedef unsigned char  UINT8;
typedef unsigned short UINT16;
typedef unsigned int   UINT32;
typedef unsigned long long UINT64;

#define SYS_EXIT          0
#define SYS_CREATE_WINDOW 1
#define SYS_DRAW_RECT     2
#define SYS_DRAW_TEXT     3
#define SYS_GET_EVENT     4
#define SYS_MARK_DIRTY    5
#define SYS_HEAP_ALLOC    6
#define SYS_READ_FILE     7
#define SYS_WRITE_FILE    8
#define SYS_LIST_FILES    9
#define SYS_EXEC_APP      10
#define VELO_EV_NONE    0
#define VELO_EV_CLICK   1
#define VELO_EV_KEY     2
#define VELO_EV_RESIZE  3
#define VELO_EV_RCLICK  4

typedef struct {
    int type;
    int x;
    int y;
    char key;
} UserEvent;

static inline UINT64 velo_syscall(UINT64 num, UINT64 a1, UINT64 a2, UINT64 a3, UINT64 a4) {
    register UINT64 r_num __asm__("rdi") = num;
    register UINT64 r_a1  __asm__("rsi") = a1;
    register UINT64 r_a2  __asm__("rdx") = a2;
    register UINT64 r_a3  __asm__("rcx") = a3;
    register UINT64 r_a4  __asm__("r8")  = a4;
    UINT64 ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "r"(r_num), "r"(r_a1), "r"(r_a2), "r"(r_a3), "r"(r_a4)
        : "memory", "r11"
    );
    return ret;
}

static inline void velo_exit(void) {
    velo_syscall(SYS_EXIT, 0, 0, 0, 0);
}

static inline int velo_create_window(const char *title, int w, int h) {
    return (int)velo_syscall(SYS_CREATE_WINDOW, (UINT64)title, (UINT64)w, (UINT64)h, 0);
}

static inline void velo_draw_rect(int win_id, int x, int y, int w, int h) {
    UINT64 packed = ((UINT64)w << 32) | (UINT32)h;
    velo_syscall(SYS_DRAW_RECT, (UINT64)win_id, (UINT64)x, (UINT64)y, packed);
}

static inline void velo_draw_text(int win_id, const char *text, int x, int y) {
    velo_syscall(SYS_DRAW_TEXT, (UINT64)win_id, (UINT64)text, (UINT64)x, (UINT64)y);
}

static inline int velo_get_event(int win_id, UserEvent *ev) {
    return (int)velo_syscall(SYS_GET_EVENT, (UINT64)win_id, (UINT64)ev, 0, 0);
}

static inline void velo_mark_dirty(void) {
    velo_syscall(SYS_MARK_DIRTY, 0, 0, 0, 0);
}

#endif
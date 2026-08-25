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

#define SYSCALL_VECTOR_ADDR 0x80000ULL

typedef struct {
    int type; // 1 = Click, 2 = Key
    int x;
    int y;
    char key;
} UserEvent;

typedef UINT64 (*velo_syscall_ptr_t)(UINT64, UINT64, UINT64, UINT64, UINT64);

static inline UINT64 velo_syscall(UINT64 num, UINT64 a1, UINT64 a2, UINT64 a3, UINT64 a4) {
    velo_syscall_ptr_t fn = *(velo_syscall_ptr_t*)(SYSCALL_VECTOR_ADDR);
    if (!fn) return (UINT64)-1;
    return fn(num, a1, a2, a3, a4);
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
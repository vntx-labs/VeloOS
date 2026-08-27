#ifndef _VELO_SYSCALL_H
#define _VELO_SYSCALL_H

typedef unsigned char      UINT8;
typedef unsigned short     UINT16;
typedef unsigned int       UINT32;
typedef unsigned long long UINT64;
typedef long long          INT64;

#define SYS_EXIT           0
#define SYS_CREATE_WINDOW  1
#define SYS_DRAW_RECT      2
#define SYS_DRAW_TEXT      3
#define SYS_GET_EVENT      4
#define SYS_MARK_DIRTY     5
#define SYS_HEAP_ALLOC     6
#define SYS_READ_FILE      7
#define SYS_WRITE_FILE     8
#define SYS_LIST_FILES     9
#define SYS_EXEC_APP       10
#define SYS_CLEAR_WINDOW   11
#define SYS_DRAW_RECT_COL  12
#define SYS_DRAW_GRADIENT  13
#define SYS_DRAW_TEXT_COL  14
#define SYS_GET_WIN_SIZE   15
#define SYS_GET_SYSINFO    16
#define SYS_GET_DRIVE_INFO 17
#define SYS_SOCKET_OPEN    18
#define SYS_SOCKET_SEND    19
#define SYS_SOCKET_RECV    20
#define SYS_SOCKET_CLOSE   21
#define SYS_HTTP_GET       22
#define SYS_DNS_RESOLVE    23
#define SYS_TASK_SLEEP     24
#define SYS_TASK_YIELD     25
#define SYS_DELETE_FILE    26
#define SYS_COPY_FILE      27
#define SYS_MOVE_FILE      28
#define SYS_MKDIR          29
#define SYS_RENAME_FILE    30
#define SYS_DRAW_ICON      31
#define VELO_EV_NONE    0
#define VELO_EV_CLICK   1
#define VELO_EV_KEY     2
#define VELO_EV_RESIZE  3
#define VELO_EV_RCLICK  4

typedef struct {
    char   name[32];
    UINT32 size;
    UINT16 date;
    UINT16 time;
    UINT8  is_dir;
    UINT8  attr;
} VeloDirEntry;

typedef struct {
    char cpu_brand[49];
    char pc_name[32];
    char user_name[32];
    UINT64 total_ram_mb;
} VeloSysInfo;

typedef struct {
    char model[41];
    char label[16];
    UINT64 total_bytes;
    UINT64 free_bytes;
    int is_removable;
} VeloDriveInfo;

typedef struct {
    int type;
    int x;
    int y;
    char key;
} velo_event_t;

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

static inline int velo_list_dir(const char *path, VeloDirEntry *out_entries, int max_entries) {
    return (int)velo_syscall(SYS_LIST_FILES, (UINT64)path, (UINT64)out_entries, (UINT64)max_entries, 0);
}

static inline int velo_read_file(const char *filename, void *buf, UINT32 max_len) {
    return (int)velo_syscall(SYS_READ_FILE, (UINT64)filename, (UINT64)buf, (UINT64)max_len, 0);
}

static inline int velo_write_file(const char *filename, void *buf, UINT32 size) {
    return (int)velo_syscall(SYS_WRITE_FILE, (UINT64)filename, (UINT64)buf, (UINT64)size, 0);
}

static inline int velo_delete_file(const char *filename) {
    return (int)velo_syscall(SYS_DELETE_FILE, (UINT64)filename, 0, 0, 0);
}

static inline int velo_copy_file(const char *src, const char *dst) {
    return (int)velo_syscall(SYS_COPY_FILE, (UINT64)src, (UINT64)dst, 0, 0);
}

static inline int velo_move_file(const char *src, const char *dst) {
    return (int)velo_syscall(SYS_MOVE_FILE, (UINT64)src, (UINT64)dst, 0, 0);
}

static inline int velo_rename_file(const char *old_path, const char *new_name) {
    return (int)velo_syscall(SYS_RENAME_FILE, (UINT64)old_path, (UINT64)new_name, 0, 0);
}

static inline int velo_mkdir(const char *path) {
    return (int)velo_syscall(SYS_MKDIR, (UINT64)path, 0, 0, 0);
}

static inline int velo_create_file(const char *filename) {
    return (int)velo_syscall(SYS_WRITE_FILE, (UINT64)filename, (UINT64)"", 0, 0);
}

static inline int velo_exec(const char *filename) {
    return (int)velo_syscall(SYS_EXEC_APP, (UINT64)filename, 0, 0, 0);
}

static inline int velo_get_sysinfo(VeloSysInfo *info) {
    return (int)velo_syscall(SYS_GET_SYSINFO, (UINT64)info, 0, 0, 0);
}

static inline int velo_get_drive_info(int drive_idx, VeloDriveInfo *info) {
    return (int)velo_syscall(SYS_GET_DRIVE_INFO, (UINT64)drive_idx, (UINT64)info, 0, 0);
}

#endif
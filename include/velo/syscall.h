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

#define SYSCALL_VECTOR_ADDR 0x80000ULL

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
    int type; // 1 = Click, 2 = Key, 3 = Resize
    int x;    // Fenster-relativ
    int y;
    char key;
} velo_event_t;

typedef UINT64 (*velo_syscall_ptr_t)(UINT64, UINT64, UINT64, UINT64, UINT64);

static inline UINT64 velo_syscall(UINT64 num, UINT64 a1, UINT64 a2, UINT64 a3, UINT64 a4) {
    velo_syscall_ptr_t fn = *(velo_syscall_ptr_t*)(SYSCALL_VECTOR_ADDR);
    if (!fn) return (UINT64)-1;
    return fn(num, a1, a2, a3, a4);
}

static inline int velo_list_dir(const char *path, VeloDirEntry *out_entries, int max_entries) {
    return (int)velo_syscall(SYS_LIST_FILES, (UINT64)path, (UINT64)out_entries, (UINT64)max_entries, 0);
}

static inline int velo_read_file(const char *filename, void *buf, UINT32 max_len) {
    return (int)velo_syscall(SYS_READ_FILE, (UINT64)filename, (UINT64)buf, (UINT64)max_len, 0);
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
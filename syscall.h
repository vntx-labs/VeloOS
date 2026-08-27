#ifndef SYSCALL_H
#define SYSCALL_H

#include <efi.h>
#include <efilib.h>

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

#define SYSCALL_VECTOR_ADDR 0x80000ULL

typedef struct {
    int type; // 1 = Click, 2 = Key, 3 = Resize
    int x;
    int y;
    char key;
} UserEvent;

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

void init_ring3_and_syscalls(void);
int load_and_run_app(const char *filename);

#endif
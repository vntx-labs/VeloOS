#ifndef SYSCALL_H
#define SYSCALL_H

#include <efi.h>
#include <efilib.h>

#define SYS_EXIT          0
#define SYS_CREATE_WINDOW 1
#define SYS_DRAW_RECT     2
#define SYS_DRAW_TEXT     3
#define SYS_GET_EVENT     4
#define SYS_MARK_DIRTY    5

typedef struct {
    int type; // 1 = Click, 2 = Key
    int x;    // Fenster-relativ
    int y;
    char key;
} UserEvent;

void init_ring3_and_syscalls(void);
int load_and_run_app(const char *filename);

#endif
#ifndef SYSCALL_H
#define SYSCALL_H

#include <efi.h>
#include <efilib.h>
#include <velo/syscall.h>

void enable_user_paging(void);
void init_ring3_and_syscalls(void);
int  load_and_run_app(const char *filename);
UINT64 syscall_handler_c(UINT64 num, UINT64 a1, UINT64 a2, UINT64 a3, UINT64 a4);

#endif /* SYSCALL_H */
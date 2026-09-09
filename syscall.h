#ifndef SYSCALL_H
#define SYSCALL_H

#include <efi.h>
#include <efilib.h>
#include <velo/syscall.h>

void enable_user_paging(void);
void init_ring3_and_syscalls(void);
int  load_and_run_app(const char *filename);

#endif
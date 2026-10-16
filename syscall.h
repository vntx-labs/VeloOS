#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include <velo/syscall.h>

#ifndef UINT32
typedef uint8_t   UINT8;
typedef uint16_t  UINT16;
typedef uint32_t  UINT32;
typedef uint64_t  UINT64;
typedef int64_t   INT64;
typedef uint64_t  UINTN;
typedef void      VOID;
#endif

void enable_user_paging(void);
void init_ring3_and_syscalls(void);
int  load_and_run_app(const char *filename);
UINT64 syscall_handler_c(UINT64 num, UINT64 a1, UINT64 a2, UINT64 a3, UINT64 a4);

#endif /* SYSCALL_H */
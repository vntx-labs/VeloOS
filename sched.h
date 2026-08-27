#ifndef SCHED_H
#define SCHED_H

#include <efi.h>
#include <efilib.h>

#define MAX_TASKS 32
#define TASK_STACK_SIZE 131072
#define APIC_TIMER_VECTOR 0x40
#define SYSCALL_INT_VECTOR 0x80

// GDT Segment-Selektoren
#define KERNEL_CS 0x08
#define KERNEL_DS 0x10
#define USER_DS   0x1B // 0x18 | 3 (RPL=3)
#define USER_CS   0x23 // 0x20 | 3 (RPL=3)

#define TASK_UNUSED   0
#define TASK_READY    1
#define TASK_RUNNING  2
#define TASK_SLEEPING 3
#define TASK_DEAD     4

typedef void (*TaskEntry)(void);

typedef struct __attribute__((aligned(64))) {
    UINT8 fxsave_area[512] __attribute__((aligned(64))); // Am Anfang für garantiertes 64-Byte Alignment
    int id;
    char name[32];
    UINT64 rsp;
    UINT8 *stack_base;
    UINT8 *user_stack_base;
    int is_user;
    int state;
    UINT32 sleep_ticks;
    UINT32 ticks_run;
    char app_path[64];
} Task;

// 64-Bit Task State Segment (TSS)
typedef struct __attribute__((packed)) {
    UINT32 reserved0;
    UINT64 rsp0; // Kernel Stack Pointer bei Interrupt/Syscall aus Ring 3
    UINT64 rsp1;
    UINT64 rsp2;
    UINT64 reserved1;
    UINT64 ist1;
    UINT64 ist2;
    UINT64 ist3;
    UINT64 ist4;
    UINT64 ist5;
    UINT64 ist6;
    UINT64 ist7;
    UINT64 reserved2;
    UINT16 reserved3;
    UINT16 iomap_base;
} TSS64;

void sched_init(void);
void sched_start(void);
int  task_create(const char *name, TaskEntry entry);
int  task_create_user(const char *name, UINT64 entry_point);
int  task_spawn_app(const char *filename);
void task_yield(void);
void task_sleep(UINT32 ticks);
void task_exit(void);
int  task_get_current_id(void);
int  task_get_count(void);

#endif
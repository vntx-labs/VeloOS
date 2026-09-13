#ifndef SCHED_H
#define SCHED_H

#include <efi.h>
#include <efilib.h>
#include <velo/syscall.h>

#define MAX_TASKS 8
#define TASK_STACK_SIZE 1048576 // 1 MB Stack pro Task
#define TASK_EVENT_QUEUE_SIZE 32
#define APIC_TIMER_VECTOR 0x40
#define SYSCALL_INT_VECTOR 0x80

#define KERNEL_CS 0x08
#define KERNEL_DS 0x10
#define USER_DS   0x1B
#define USER_CS   0x23
#define TSS_SEL   0x28

#define TASK_UNUSED   0
#define TASK_READY    1
#define TASK_RUNNING  2
#define TASK_SLEEPING 3
#define TASK_DEAD     4

typedef void (*TaskEntry)(void);

typedef struct {
    UserEvent buffer[TASK_EVENT_QUEUE_SIZE];
    UINT32 head;
    UINT32 tail;
} TaskEventQueue;

typedef struct __attribute__((aligned(64))) {
    UINT8 fxsave_area[512] __attribute__((aligned(64)));
    int id;
    char name[32];
    UINT64 rsp;
    UINT8 *stack_base;
    UINT8 *user_stack_base;
    int is_user;
    int state;
    UINT32 sleep_ticks;
    UINT32 ticks_run;
    
    UINT64 *cr3_pml4;
    UINT64 heap_start;
    UINT64 heap_break;
    UINT64 heap_max;

    TaskEventQueue event_queue;
} Task;

typedef struct __attribute__((packed)) {
    UINT32 reserved0;
    UINT64 rsp0;
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
int  task_create_user(const char *name, UINT64 entry_point, UINT64 *cr3_pml4, UINT64 heap_start, UINT64 heap_max);
int  task_spawn_app(const char *filename);
void task_yield(void);
void task_sleep(UINT32 ticks);
void task_exit(void);
int  task_get_current_id(void);
int  task_get_count(void);
Task* task_get_current(void);

/* Prozess-spezifisches Input-Routing */
void task_push_event(int task_id, int type, int x, int y, char key);
int  task_pop_event(int task_id, UserEvent *out_ev);

#endif /* SCHED_H */
#include "sched.h"
#include "syscall.h"
#include "pmm_vmm.h"
#include "font.h"
#include "wm.h"

extern EFI_SYSTEM_TABLE* g_st;
void swap_buffers(void);

#define LAPIC_ID            0x0020
#define LAPIC_EOI           0x00B0
#define LAPIC_SVR           0x00F0
#define LAPIC_LVT_TIMER     0x0320
#define LAPIC_TIMER_INITCNT 0x0380
#define LAPIC_TIMER_CURRCNT 0x0390
#define LAPIC_TIMER_DIV     0x03E0

static UINTN g_lapic_base = 0xFEE00000ULL;
static int g_multitasking_active = 0;
Task g_tasks[MAX_TASKS] __attribute__((aligned(64)));
static int g_current_task_idx = 0;
static int g_total_tasks = 1;
static UINT64 g_kernel_cr3 = 0;

static UINT8 g_task_kernel_stacks[MAX_TASKS][TASK_STACK_SIZE] __attribute__((aligned(4096)));
static UINT8 g_task_user_stacks[MAX_TASKS][TASK_STACK_SIZE]   __attribute__((aligned(4096)));
static UINT8 g_exception_stack[16384]                         __attribute__((aligned(16)));

typedef struct __attribute__((packed)) {
    UINT16 limit_low;
    UINT16 base_low;
    UINT8  base_mid;
    UINT8  access;
    UINT8  granularity;
    UINT8  base_high;
    UINT32 base_upper;
    UINT32 reserved;
} GdtTssEntry;

typedef struct __attribute__((packed)) {
    UINT16 limit;
    UINT64 base;
} GdtPtr;

typedef struct __attribute__((packed)) {
    UINT16 offset_low;
    UINT16 selector;
    UINT8  ist;
    UINT8  type_attr;
    UINT16 offset_mid;
    UINT32 offset_high;
    UINT32 zero;
} IdtEntry;

typedef struct __attribute__((packed)) {
    UINT16 limit;
    UINT64 base;
} IdtPtr;

static UINT64 g_gdt[8] __attribute__((aligned(64)));
static TSS64 g_tss __attribute__((aligned(64)));
static IdtEntry g_idt[256] __attribute__((aligned(16)));

typedef struct __attribute__((packed)) {
    UINT64 r15, r14, r13, r12, r11, r10, r9, r8;
    UINT64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    UINT64 vector;
    UINT64 error_code;
    UINT64 rip;
    UINT64 cs;
    UINT64 rflags;
    UINT64 rsp;
    UINT64 ss;
} ExceptionContext;

static inline void lapic_write(UINT32 reg, UINT32 val) {
    *(volatile UINT32*)(g_lapic_base + reg) = val;
}

static inline UINT64 rdmsr(UINT32 msr) {
    UINT32 low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((UINT64)high << 32) | low;
}

static inline unsigned char inb_cmos_sched(unsigned short port) {
    unsigned char res;
    __asm__ volatile("inb %1, %0" : "=a"(res) : "Nd"(port));
    return res;
}

static inline void outb_cmos_sched(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static unsigned char get_rtc_second(void) {
    outb_cmos_sched(0x70, 0x00);
    return inb_cmos_sched(0x71);
}

static UINT32 calibrate_apic_timer(void) {
    lapic_write(LAPIC_TIMER_DIV, 0x03);
    lapic_write(LAPIC_LVT_TIMER, (1U << 16));

    unsigned char s = get_rtc_second();
    int timeout = 2000000;
    while (get_rtc_second() == s && --timeout) __asm__ volatile("pause");

    lapic_write(LAPIC_TIMER_INITCNT, 0xFFFFFFFF);
    unsigned char s_start = get_rtc_second();
    timeout = 2000000;
    while (get_rtc_second() == s_start && --timeout) __asm__ volatile("pause");

    UINT32 curr = *(volatile UINT32*)(g_lapic_base + LAPIC_TIMER_CURRCNT);
    UINT32 ticks_per_sec = 0xFFFFFFFF - curr;

    if (ticks_per_sec == 0 || ticks_per_sec > 0xFFFFFFF0 || timeout == 0) {
        ticks_per_sec = 8000000;
    }

    UINT32 init_cnt = ticks_per_sec / 100;
    if (init_cnt < 0x2000) init_cnt = 0x2000;
    if (init_cnt > 0x0FFFFFFF) init_cnt = 0x0FFFFFFF;
    return init_cnt;
}

void kernel_exception_handler_c(ExceptionContext *ctx) {
    if (ctx->vector == 14) {
        UINT64 fault_cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(fault_cr2));
        Task *cur = &g_tasks[g_current_task_idx];

        if (cur->is_user && cur->cr3_pml4) {
            if (vmm_handle_page_fault(cur->cr3_pml4, fault_cr2, cur->heap_start, cur->heap_max)) {
                return;
            }
        }
    }

    task_exit();
}

__attribute__((naked, used)) static void common_exc_stub(void) {
    __asm__ volatile(
        "cld\n\t"
        "pushq %rax; pushq %rbx; pushq %rcx; pushq %rdx\n\t"
        "pushq %rsi; pushq %rdi; pushq %rbp; pushq %r8\n\t"
        "pushq %r9;  pushq %r10; pushq %r11; pushq %r12\n\t"
        "pushq %r13; pushq %r14; pushq %r15\n\t"
        "movq %rsp, %rdi\n\t"
        "call kernel_exception_handler_c\n\t"
        "popq %r15; popq %r14; popq %r13; popq %r12\n\t"
        "popq %rbp; popq %rbx; popq %r11; popq %r10\n\t"
        "popq %r9;  popq %r8;  popq %rcx; popq %rdx\n\t"
        "popq %rsi; popq %rdi; popq %rax\n\t"
        "addq $16, %rsp\n\t"
        "iretq\n\t"
    );
}

__attribute__((naked)) static void exc_stub_0(void)  { __asm__ volatile("pushq $0; pushq $0; jmp common_exc_stub\n\t"); }
__attribute__((naked)) static void exc_stub_6(void)  { __asm__ volatile("pushq $0; pushq $6; jmp common_exc_stub\n\t"); }
__attribute__((naked)) static void exc_stub_8(void)  { __asm__ volatile("pushq $8; jmp common_exc_stub\n\t"); }
__attribute__((naked)) static void exc_stub_13(void) { __asm__ volatile("pushq $13; jmp common_exc_stub\n\t"); }
__attribute__((naked)) static void exc_stub_14(void) { __asm__ volatile("pushq $14; jmp common_exc_stub\n\t"); }

__attribute__((naked)) static void syscall_int80_isr(void) {
    __asm__ volatile(
        "cld\n\t"
        "pushq %rdi; pushq %rsi; pushq %rdx; pushq %rcx\n\t"
        "pushq %r8;  pushq %r9;  pushq %r10; pushq %r11\n\t"
        "pushq %rbx; pushq %rbp; pushq %r12; pushq %r13\n\t"
        "pushq %r14; pushq %r15\n\t"
        "movw $0x10, %ax; movw %ax, %ds; movw %ax, %es\n\t"
        "call syscall_handler_c\n\t"
        "movw $0x1B, %dx; movw %dx, %ds; movw %dx, %es\n\t"
        "popq %r15; popq %r14; popq %r13; popq %r12\n\t"
        "popq %rbp; popq %rbx; popq %r11; popq %r10\n\t"
        "popq %r9;  popq %r8;  popq %rcx; popq %rdx\n\t"
        "popq %rsi; popq %rdi\n\t"
        "iretq\n\t"
    );
}

UINT64 sched_schedule_c(UINT64 current_rsp) {
    if (!g_multitasking_active) {
        lapic_write(LAPIC_EOI, 0);
        return current_rsp;
    }

    g_tasks[g_current_task_idx].rsp = current_rsp;
    __asm__ volatile("fxsave64 (%0)" : : "r"(&g_tasks[g_current_task_idx].fxsave_area[0]) : "memory");
    g_tasks[g_current_task_idx].ticks_run++;

    for (int i = 0; i < MAX_TASKS; i++) {
        if (g_tasks[i].state == TASK_SLEEPING) {
            if (g_tasks[i].sleep_ticks > 0) g_tasks[i].sleep_ticks--;
            if (g_tasks[i].sleep_ticks == 0) g_tasks[i].state = TASK_READY;
        }
    }

    int next = g_current_task_idx;
    for (int i = 0; i < MAX_TASKS; i++) {
        next = (next + 1) % MAX_TASKS;
        if (g_tasks[next].state == TASK_READY || g_tasks[next].state == TASK_RUNNING) {
            break;
        }
    }

    if (g_tasks[g_current_task_idx].state == TASK_RUNNING && g_current_task_idx != next) {
        g_tasks[g_current_task_idx].state = TASK_READY;
    }

    g_current_task_idx = next;
    g_tasks[g_current_task_idx].state = TASK_RUNNING;
    g_tss.rsp0 = (UINT64)(g_tasks[g_current_task_idx].stack_base + TASK_STACK_SIZE);

    UINT64 target_cr3 = g_tasks[g_current_task_idx].cr3_pml4 ? 
                        (UINT64)(UINTN)g_tasks[g_current_task_idx].cr3_pml4 : g_kernel_cr3;
    __asm__ volatile("mov %0, %%cr3" : : "r"(target_cr3) : "memory");

    __asm__ volatile("fxrstor64 (%0)" : : "r"(&g_tasks[g_current_task_idx].fxsave_area[0]) : "memory");

    lapic_write(LAPIC_EOI, 0);
    return g_tasks[g_current_task_idx].rsp;
}

__attribute__((naked)) void apic_timer_isr(void) {
    __asm__ volatile(
        "cld\n\t"
        "pushq %rax; pushq %rbx; pushq %rcx; pushq %rdx\n\t"
        "pushq %rsi; pushq %rdi; pushq %rbp; pushq %r8\n\t"
        "pushq %r9;  pushq %r10; pushq %r11; pushq %r12\n\t"
        "pushq %r13; pushq %r14; pushq %r15\n\t"
        "movw $0x10, %ax; movw %ax, %ds; movw %ax, %es\n\t"
        "movq %rsp, %rdi\n\t"
        "call sched_schedule_c\n\t"
        "movq %rax, %rsp\n\t"
        "popq %r15; popq %r14; popq %r13; popq %r12\n\t"
        "popq %rbp; popq %rbx; popq %r11; popq %r10\n\t"
        "popq %r9;  popq %r8;  popq %rcx; popq %rdx\n\t"
        "popq %rsi; popq %rdi; popq %rax\n\t"
        "iretq\n\t"
    );
}

static void register_idt_entry(int vector, UINT64 isr_addr, UINT8 ist, UINT8 dpl) {
    UINT8 type_attr = (UINT8)(0x8E | (dpl << 5));
    g_idt[vector].offset_low = (UINT16)(isr_addr & 0xFFFF);
    g_idt[vector].selector = KERNEL_CS;
    g_idt[vector].ist = ist;
    g_idt[vector].type_attr = type_attr;
    g_idt[vector].offset_mid = (UINT16)((isr_addr >> 16) & 0xFFFF);
    g_idt[vector].offset_high = (UINT32)(isr_addr >> 32);
    g_idt[vector].zero = 0;
}

static void init_gdt_and_tss(void) {
    __builtin_memset(g_gdt, 0, sizeof(g_gdt));
    __builtin_memset(&g_tss, 0, sizeof(g_tss));

    g_gdt[0] = 0x0000000000000000ULL;
    g_gdt[1] = 0x00AF9A000000FFFFULL; // Kernel CS (0x08)
    g_gdt[2] = 0x00CF92000000FFFFULL; // Kernel DS (0x10)
    g_gdt[3] = 0x00CFF2000000FFFFULL; // User DS   (0x18 | 3 = 0x1B)
    g_gdt[4] = 0x00AFFA000000FFFFULL; // User CS   (0x20 | 3 = 0x23)

    GdtTssEntry *tss_desc = (GdtTssEntry*)&g_gdt[5];
    UINT64 tss_base = (UINT64)(UINTN)&g_tss;
    UINT32 tss_limit = sizeof(TSS64) - 1;

    tss_desc->limit_low = (UINT16)(tss_limit & 0xFFFF);
    tss_desc->base_low = (UINT16)(tss_base & 0xFFFF);
    tss_desc->base_mid = (UINT8)((tss_base >> 16) & 0xFF);
    tss_desc->access = 0x89;
    tss_desc->granularity = 0x00;
    tss_desc->base_high = (UINT8)((tss_base >> 24) & 0xFF);
    tss_desc->base_upper = (UINT32)(tss_base >> 32);
    tss_desc->reserved = 0;

    g_tss.ist1 = (UINT64)(g_exception_stack + sizeof(g_exception_stack));
    g_tss.rsp0 = (UINT64)(g_task_kernel_stacks[0] + TASK_STACK_SIZE);
    g_tss.iomap_base = sizeof(TSS64);

    GdtPtr gdtr;
    gdtr.limit = sizeof(g_gdt) - 1;
    gdtr.base = (UINT64)(UINTN)g_gdt;

    __asm__ volatile("lgdt %0" : : "m"(gdtr));
    __asm__ volatile("ltr %0" : : "r"((UINT16)TSS_SEL));
    __asm__ volatile("movw %0, %%ds; movw %0, %%es; movw %0, %%ss" : : "r"((UINT16)KERNEL_DS));
}

int task_create(const char *name, TaskEntry entry) {
    if (!entry) return -1;
    int slot = -1;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (g_tasks[i].state == TASK_UNUSED || g_tasks[i].state == TASK_DEAD) {
            slot = i; break;
        }
    }
    if (slot == -1) return -1;

    Task *t = &g_tasks[slot];
    t->id = slot;
    t->is_user = 0;
    t->cr3_pml4 = NULL;
    t->stack_base = g_task_kernel_stacks[slot];
    t->user_stack_base = NULL;
    int p = 0; while (name && name[p] && p < 31) { t->name[p] = name[p]; p++; } t->name[p] = '\0';

    __builtin_memset(t->fxsave_area, 0, sizeof(t->fxsave_area));
    *(UINT16*)&t->fxsave_area[0] = 0x037F;
    *(UINT32*)&t->fxsave_area[24] = 0x1F80;

    UINT64 *sp = (UINT64*)(t->stack_base + TASK_STACK_SIZE);
    sp = (UINT64*)((UINT64)sp & ~0xFULL);

    *(--sp) = (UINT64)KERNEL_DS;
    *(--sp) = (UINT64)(t->stack_base + TASK_STACK_SIZE - 16);
    *(--sp) = 0x202;
    *(--sp) = (UINT64)KERNEL_CS;
    *(--sp) = (UINT64)entry;
    for (int r = 0; r < 15; r++) *(--sp) = 0;

    t->rsp = (UINT64)sp;
    t->state = TASK_READY;
    t->sleep_ticks = 0;
    t->ticks_run = 0;
    g_total_tasks++;
    return slot;
}

int task_create_user(const char *name, UINT64 entry_point, UINT64 *cr3_pml4, UINT64 heap_start, UINT64 heap_max) {
    if (!entry_point) return -1;
    int slot = -1;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (g_tasks[i].state == TASK_UNUSED || g_tasks[i].state == TASK_DEAD) {
            slot = i; break;
        }
    }
    if (slot == -1) return -1;

    Task *t = &g_tasks[slot];
    t->id = slot;
    t->is_user = 1;
    t->cr3_pml4 = cr3_pml4;
    t->heap_start = heap_start;
    t->heap_break = heap_start;
    t->heap_max = heap_max;
    t->stack_base = g_task_kernel_stacks[slot];
    t->user_stack_base = g_task_user_stacks[slot];
    int p = 0; while (name && name[p] && p < 31) { t->name[p] = name[p]; p++; } t->name[p] = '\0';

    __builtin_memset(t->fxsave_area, 0, sizeof(t->fxsave_area));
    *(UINT16*)&t->fxsave_area[0] = 0x037F;
    *(UINT32*)&t->fxsave_area[24] = 0x1F80;

    // User-Stack 16-Byte ausrichten
    UINT64 user_rsp = (UINT64)(t->user_stack_base + TASK_STACK_SIZE - 64);
    user_rsp &= ~0xFULL;

    // Kernel-Stack vorbereiten
    UINT64 *sp = (UINT64*)(t->stack_base + TASK_STACK_SIZE);
    sp = (UINT64*)((UINT64)sp & ~0xFULL);

    // ECHTES X86_64 IRETQ FRAME FUER RING 3 (SS, RSP, RFLAGS, CS, RIP)
    *(--sp) = (UINT64)USER_DS;       // SS = 0x1B (0x18 | 3)
    *(--sp) = user_rsp;              // RSP im Userland
    *(--sp) = 0x202;                 // RFLAGS (IF=1)
    *(--sp) = (UINT64)USER_CS;       // CS = 0x23 (0x20 | 3)
    *(--sp) = (UINT64)entry_point;   // RIP (Startadresse)

    // 15 Universalregister für Context-Switch
    for (int r = 0; r < 15; r++) *(--sp) = 0;

    t->rsp = (UINT64)sp;
    t->state = TASK_READY;
    t->sleep_ticks = 0;
    t->ticks_run = 0;
    g_total_tasks++;
    return slot;
}

int task_spawn_app(const char *filename) {
    if (!filename) return -1;
    return load_and_run_app(filename);
}

void task_yield(void) {
    __asm__ volatile("sti; pause");
}

void task_sleep(UINT32 ticks) {
    if (ticks == 0) return;
    g_tasks[g_current_task_idx].sleep_ticks = ticks;
    g_tasks[g_current_task_idx].state = TASK_SLEEPING;
    while (g_tasks[g_current_task_idx].state == TASK_SLEEPING) {
        __asm__ volatile("sti; pause");
    }
}

void task_exit(void) {
    Task *cur = &g_tasks[g_current_task_idx];
    cur->state = TASK_DEAD;
    
    if (cur->cr3_pml4) {
        vmm_destroy_address_space(cur->cr3_pml4);
        cur->cr3_pml4 = NULL;
    }

    if (g_total_tasks > 1) g_total_tasks--;
    while (1) {
        __asm__ volatile("sti; pause");
    }
}

int task_get_current_id(void) { return g_current_task_idx; }
int task_get_count(void) { return g_total_tasks; }
Task* task_get_current(void) { return &g_tasks[g_current_task_idx]; }

void sched_init(void) {
    __builtin_memset(g_tasks, 0, sizeof(g_tasks));
    g_tasks[0].id = 0;
    __builtin_memcpy(g_tasks[0].name, "KernelIdle", 11);
    g_tasks[0].state = TASK_RUNNING;
    g_tasks[0].stack_base = g_task_kernel_stacks[0];
    g_tasks[0].cr3_pml4 = NULL;
    *(UINT16*)&g_tasks[0].fxsave_area[0] = 0x037F;
    *(UINT32*)&g_tasks[0].fxsave_area[24] = 0x1F80;
    g_current_task_idx = 0;
    g_total_tasks = 1;

    UINT64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    g_kernel_cr3 = cr3 & 0x000FFFFFFFFFF000ULL;

    init_gdt_and_tss();

    UINT64 apic_msr = rdmsr(0x1B);
    g_lapic_base = apic_msr & 0xFFFFF000ULL;

    IdtPtr old_idtr;
    __asm__ volatile("sidt %0" : "=m"(old_idtr));
    if (old_idtr.base && old_idtr.limit > 0) {
        UINTN limit_bytes = (UINTN)old_idtr.limit + 1;
        UINTN copy_sz = limit_bytes < sizeof(g_idt) ? limit_bytes : sizeof(g_idt);
        __builtin_memcpy(g_idt, (void*)old_idtr.base, copy_sz);
    }

    register_idt_entry(0,  (UINT64)exc_stub_0,  1, 0);
    register_idt_entry(6,  (UINT64)exc_stub_6,  1, 0);
    register_idt_entry(8,  (UINT64)exc_stub_8,  1, 0);
    register_idt_entry(13, (UINT64)exc_stub_13, 1, 0);
    register_idt_entry(14, (UINT64)exc_stub_14, 1, 0);

    register_idt_entry(APIC_TIMER_VECTOR, (UINT64)apic_timer_isr, 0, 0);
    register_idt_entry(SYSCALL_INT_VECTOR, (UINT64)syscall_int80_isr, 0, 3);

    IdtPtr new_idtr;
    new_idtr.limit = sizeof(g_idt) - 1;
    new_idtr.base = (UINT64)(UINTN)g_idt;
    __asm__ volatile("lidt %0" : : "m"(new_idtr));

    UINT32 calibrated_cnt = calibrate_apic_timer();

    lapic_write(LAPIC_SVR, 0x1FF);
    lapic_write(LAPIC_TIMER_DIV, 0x03);
    lapic_write(LAPIC_LVT_TIMER, (1U << 17) | APIC_TIMER_VECTOR);
    lapic_write(LAPIC_TIMER_INITCNT, calibrated_cnt);
}

void sched_start(void) {
    enable_user_paging();
    g_multitasking_active = 1;
    __asm__ volatile("sti");
}
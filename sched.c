#include "sched.h"
#include "syscall.h"

extern EFI_SYSTEM_TABLE* g_st;
void klog(const char *s);

#define LAPIC_ID            0x0020
#define LAPIC_EOI           0x00B0
#define LAPIC_SVR           0x00F0
#define LAPIC_LVT_TIMER     0x0320
#define LAPIC_TIMER_INITCNT 0x0380
#define LAPIC_TIMER_CURRCNT 0x0390
#define LAPIC_TIMER_DIV     0x03E0

static UINTN g_lapic_base = 0xFEE00000ULL;
static int g_multitasking_active = 0;
static Task g_tasks[MAX_TASKS] __attribute__((aligned(64)));
static int g_current_task_idx = 0;
static int g_total_tasks = 1;

static UINT8 g_task_stacks[MAX_TASKS][TASK_STACK_SIZE] __attribute__((aligned(4096)));
static UINT8 g_exception_stack[16384] __attribute__((aligned(16)));

typedef struct __attribute__((packed)) {
    UINT16 limit_low;
    UINT16 base_low;
    UINT8  base_mid;
    UINT8  access;
    UINT8  granularity;
    UINT8  base_high;
} GdtEntry;

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

static UINT8 g_gdt_storage[512] __attribute__((aligned(64)));
static TSS64 g_tss __attribute__((aligned(64)));
static IdtEntry g_idt[256] __attribute__((aligned(16)));
static UINT16 g_tss_selector = 0x28;
static UINT16 g_kernel_cs = 0x38;
static UINT16 g_kernel_ds = 0x30;

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

// ==========================================
// EXCEPTION HANDLER
// ==========================================
void kernel_exception_handler_c(ExceptionContext *ctx) {
    klog("\n[!] CPU Exception abgefangen in Task: ");
    klog(g_tasks[g_current_task_idx].name);
    klog("\n[!] Vektor: ");
    char num_buf[16];
    num_buf[0] = '0' + (char)(ctx->vector / 10);
    num_buf[1] = '0' + (char)(ctx->vector % 10);
    num_buf[2] = '\0';
    klog(num_buf);
    klog(" | RIP: ");
    for (int i = 60; i >= 0; i -= 4) {
        UINT8 h = (UINT8)((ctx->rip >> i) & 0xF);
        char c = (h < 10) ? ('0' + h) : ('A' + h - 10);
        char str[2] = {c, '\0'};
        klog(str);
    }
    klog("\n[+] Fehlerhafter Task beendet. Desktop laeuft weiter.\n");

    task_exit();
}

__attribute__((naked, used)) static void common_exc_stub(void) {
    __asm__ volatile(
        "cld\n\t"
        "pushq %rax\n\t"
        "pushq %rbx\n\t"
        "pushq %rcx\n\t"
        "pushq %rdx\n\t"
        "pushq %rsi\n\t"
        "pushq %rdi\n\t"
        "pushq %rbp\n\t"
        "pushq %r8\n\t"
        "pushq %r9\n\t"
        "pushq %r10\n\t"
        "pushq %r11\n\t"
        "pushq %r12\n\t"
        "pushq %r13\n\t"
        "pushq %r14\n\t"
        "pushq %r15\n\t"

        "movq %rsp, %rdi\n\t"
        "call kernel_exception_handler_c\n\t"

        "1: hlt; jmp 1b\n\t"
    );
}

__attribute__((naked)) static void exc_stub_0(void)  { __asm__ volatile("pushq $0; pushq $0; jmp common_exc_stub\n\t"); }
__attribute__((naked)) static void exc_stub_6(void)  { __asm__ volatile("pushq $0; pushq $6; jmp common_exc_stub\n\t"); }
__attribute__((naked)) static void exc_stub_13(void) { __asm__ volatile("pushq $13; jmp common_exc_stub\n\t"); }
__attribute__((naked)) static void exc_stub_14(void) { __asm__ volatile("pushq $14; jmp common_exc_stub\n\t"); }

// ==========================================
// INT 0x80 SYSCALL HANDLER
// ==========================================
__attribute__((naked)) static void syscall_int80_isr(void) {
    __asm__ volatile(
        "cld\n\t"
        "pushq %rdi\n\t"
        "pushq %rsi\n\t"
        "pushq %rdx\n\t"
        "pushq %rcx\n\t"
        "pushq %r8\n\t"
        "pushq %r9\n\t"
        "pushq %r10\n\t"
        "pushq %r11\n\t"
        "pushq %rbx\n\t"
        "pushq %rbp\n\t"
        "pushq %r12\n\t"
        "pushq %r13\n\t"
        "pushq %r14\n\t"
        "pushq %r15\n\t"

        "call syscall_handler_c\n\t"

        "popq %r15\n\t"
        "popq %r14\n\t"
        "popq %r13\n\t"
        "popq %r12\n\t"
        "popq %rbp\n\t"
        "popq %rbx\n\t"
        "popq %r11\n\t"
        "popq %r10\n\t"
        "popq %r9\n\t"
        "popq %r8\n\t"
        "popq %rcx\n\t"
        "popq %rdx\n\t"
        "popq %rsi\n\t"
        "popq %rdi\n\t"
        "iretq\n\t"
    );
}

// ==========================================
// PREEMPTIVER SCHEDULER DISPATCHER
// ==========================================
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

    __asm__ volatile("fxrstor64 (%0)" : : "r"(&g_tasks[g_current_task_idx].fxsave_area[0]) : "memory");

    lapic_write(LAPIC_EOI, 0);
    return g_tasks[g_current_task_idx].rsp;
}

__attribute__((naked)) void apic_timer_isr(void) {
    __asm__ volatile(
        "cld\n\t"
        "pushq %rax\n\t"  // 1
        "pushq %rbx\n\t"  // 2
        "pushq %rcx\n\t"  // 3
        "pushq %rdx\n\t"  // 4
        "pushq %rsi\n\t"  // 5
        "pushq %rdi\n\t"  // 6
        "pushq %rbp\n\t"  // 7
        "pushq %r8\n\t"   // 8
        "pushq %r9\n\t"   // 9
        "pushq %r10\n\t"  // 10
        "pushq %r11\n\t"  // 11
        "pushq %r12\n\t"  // 12
        "pushq %r13\n\t"  // 13
        "pushq %r14\n\t"  // 14
        "pushq %r15\n\t"  // 15

        "movq %rsp, %rdi\n\t"
        "call sched_schedule_c\n\t"
        "movq %rax, %rsp\n\t"

        "popq %r15\n\t"   // 15
        "popq %r14\n\t"   // 14
        "popq %r13\n\t"   // 13
        "popq %r12\n\t"   // 12
        "popq %r11\n\t"   // 11
        "popq %r10\n\t"   // 10
        "popq %r9\n\t"    // 9
        "popq %r8\n\t"    // 8
        "popq %rbp\n\t"   // 7
        "popq %rdi\n\t"   // 6
        "popq %rsi\n\t"   // 5
        "popq %rdx\n\t"   // 4
        "popq %rcx\n\t"   // 3
        "popq %rbx\n\t"   // 2
        "popq %rax\n\t"   // 1

        "iretq\n\t"
    );
}

static void register_idt_entry(int vector, UINT64 isr_addr, UINT8 ist, UINT8 dpl) {
    UINT8 type_attr = (UINT8)(0x8E | (dpl << 5));

    g_idt[vector].offset_low = (UINT16)(isr_addr & 0xFFFF);
    g_idt[vector].selector = g_kernel_cs;
    g_idt[vector].ist = ist;
    g_idt[vector].type_attr = type_attr;
    g_idt[vector].offset_mid = (UINT16)((isr_addr >> 16) & 0xFFFF);
    g_idt[vector].offset_high = (UINT32)(isr_addr >> 32);
    g_idt[vector].zero = 0;
}

static void init_gdt_and_tss(void) {
    GdtPtr old_gdtr;
    __asm__ volatile("sgdt %0" : "=m"(old_gdtr));

    __builtin_memset(g_gdt_storage, 0, sizeof(g_gdt_storage));
    __builtin_memset(&g_tss, 0, sizeof(g_tss));

    UINTN copy_len = (UINTN)old_gdtr.limit + 1;
    if (copy_len > sizeof(g_gdt_storage) - 32) copy_len = sizeof(g_gdt_storage) - 32;
    __builtin_memcpy(g_gdt_storage, (void*)old_gdtr.base, copy_len);

    UINTN tss_offset = (copy_len + 7) & ~7;
    g_tss_selector = (UINT16)tss_offset;

    GdtTssEntry *tss_desc = (GdtTssEntry*)&g_gdt_storage[tss_offset];
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
    g_tss.iomap_base = sizeof(TSS64);

    GdtPtr new_gdtr;
    new_gdtr.limit = (UINT16)(tss_offset + 16 - 1);
    new_gdtr.base = (UINT64)(UINTN)g_gdt_storage;

    __asm__ volatile("lgdt %0" : : "m"(new_gdtr));
    __asm__ volatile("ltr %0" : : "r"(g_tss_selector));

    __asm__ volatile("mov %%cs, %0" : "=r"(g_kernel_cs));
    __asm__ volatile("mov %%ss, %0" : "=r"(g_kernel_ds));

    klog("[+] GDT & Hardware-TSS (IST1 Notfall-Stack) aktiv.\n");
}

int task_create(const char *name, TaskEntry entry) {
    if (!entry) return -1;

    int slot = -1;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (g_tasks[i].state == TASK_UNUSED || g_tasks[i].state == TASK_DEAD) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return -1;

    Task *t = &g_tasks[slot];
    t->id = slot;
    t->is_user = 0;
    t->stack_base = g_task_stacks[slot];
    t->user_stack_base = NULL;
    int p = 0; while (name && name[p] && p < 31) { t->name[p] = name[p]; p++; } t->name[p] = '\0';

    __builtin_memset(t->fxsave_area, 0, sizeof(t->fxsave_area));
    *(UINT16*)&t->fxsave_area[0] = 0x037F;
    *(UINT32*)&t->fxsave_area[24] = 0x1F80;

    UINT64 *sp = (UINT64*)(t->stack_base + TASK_STACK_SIZE);
    sp = (UINT64*)((UINT64)sp & ~0xFULL);

    // IRETQ Frame (5 QWORDs)
    *(--sp) = (UINT64)g_kernel_ds;
    *(--sp) = (UINT64)(t->stack_base + TASK_STACK_SIZE - 16);
    *(--sp) = 0x202; // IF=1
    *(--sp) = (UINT64)g_kernel_cs;
    *(--sp) = (UINT64)entry;

    // 15 Register
    for (int r = 0; r < 15; r++) *(--sp) = 0;

    t->rsp = (UINT64)sp;
    t->state = TASK_READY;
    t->sleep_ticks = 0;
    t->ticks_run = 0;
    g_total_tasks++;

    return slot;
}

int task_create_user(const char *name, UINT64 entry_point) {
    if (!entry_point) return -1;
    return task_create(name, (TaskEntry)(void*)entry_point);
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
    g_tasks[g_current_task_idx].state = TASK_DEAD;
    g_total_tasks--;
    while (1) {
        __asm__ volatile("sti; hlt");
    }
}

int task_get_current_id(void) {
    return g_current_task_idx;
}

int task_get_count(void) {
    return g_total_tasks;
}

void sched_init(void) {
    klog("[+] Initialisiere 64-Bit Preemptive Scheduler...\n");

    __builtin_memset(g_tasks, 0, sizeof(g_tasks));

    g_tasks[0].id = 0;
    __builtin_memcpy(g_tasks[0].name, "KernelIdle", 11);
    g_tasks[0].state = TASK_RUNNING;
    g_tasks[0].stack_base = g_task_stacks[0];
    g_current_task_idx = 0;
    g_total_tasks = 1;

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

    // Exceptions auf IST1 absichern
    register_idt_entry(0,  (UINT64)exc_stub_0,  1, 0);
    register_idt_entry(6,  (UINT64)exc_stub_6,  1, 0);
    register_idt_entry(13, (UINT64)exc_stub_13, 1, 0);
    register_idt_entry(14, (UINT64)exc_stub_14, 1, 0);

    // APIC Timer auf Vektor 0x40
    register_idt_entry(APIC_TIMER_VECTOR, (UINT64)apic_timer_isr, 0, 0);

    // Syscalls auf Vektor 0x80
    register_idt_entry(SYSCALL_INT_VECTOR, (UINT64)syscall_int80_isr, 0, 3);

    IdtPtr new_idtr;
    new_idtr.limit = sizeof(g_idt) - 1;
    new_idtr.base = (UINT64)(UINTN)g_idt;
    __asm__ volatile("lidt %0" : : "m"(new_idtr));

    lapic_write(LAPIC_SVR, 0x1FF);
    lapic_write(LAPIC_TIMER_DIV, 0x03);
    lapic_write(LAPIC_LVT_TIMER, (1U << 17) | APIC_TIMER_VECTOR);
    lapic_write(LAPIC_TIMER_INITCNT, 0x80000);

    klog("[+] APIC Timer (0x40) & Syscall Gate (int 0x80) aktiv.\n");
}

void sched_start(void) {
    g_multitasking_active = 1;
    __asm__ volatile("sti");
    klog("[+] Preemptives Multitasking aktiv.\n");
}
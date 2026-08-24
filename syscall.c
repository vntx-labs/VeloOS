// syscall.c - x86-64 Fast-Syscall Initialisierung & Dispatcher (LSTAR/SYSRET)
#include "syscall.h"
#include "wm.h"
#include "ahci.h"
#include "fat32.h"
#include "mouse.h"

extern UINTN gop_width;
extern UINTN gop_height;

void draw_filled_rect(UINTN start_x, UINTN start_y, UINTN width, UINTN height, UINT32 color);
void draw_rounded_rect_aa(int sx, int sy, int w, int h, int r, UINT32 color);
void draw_rounded_rect_gradient(int sx, int sy, int w, int h, int r, UINT32 top_col, UINT32 bot_col);

/* MSR Register */
#define MSR_EFER          0xC0000080
#define MSR_STAR          0xC0000081
#define MSR_LSTAR         0xC0000082
#define MSR_FMASK         0xC0000084

#define EFER_SCE          (1ULL << 0)

static UINT8 g_app_buffer[65536] __attribute__((aligned(4096)));
static int g_app_win_id = -1;

static inline void wrmsr(UINT32 msr, UINT64 val) {
    UINT32 low = (UINT32)val;
    UINT32 high = (UINT32)(val >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static inline UINT64 rdmsr(UINT32 msr) {
    UINT32 low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((UINT64)high << 32) | low;
}

/* Syscall Handler Dispatcher */
UINT64 syscall_handler_c(UINT64 num, UINT64 a1, UINT64 a2, UINT64 a3, UINT64 a4) {
    switch (num) {
        case SYS_EXIT: {
            if (g_app_win_id >= 0) {
                wm_close_window(g_app_win_id);
                g_app_win_id = -1;
            }
            return 0;
        }

        case SYS_CREATE_WINDOW: {
            const char *title = (const char *)a1;
            int w = (int)a2;
            int h = (int)a3;
            g_app_win_id = wm_create_window_auto(title, w, h, NULL);
            return (UINT64)g_app_win_id;
        }

        case SYS_DRAW_RECT: {
            int win_id = (int)a1;
            Window *win = wm_get_window(win_id);
            if (!win || win->is_closed) return (UINT64)-1;

            int rx = win->x + 12 + (int)a2;
            int ry = win->y + 40 + (int)a3;
            int rw = (int)(a4 >> 32);
            int rh = (int)(a4 & 0xFFFFFFFF);
            draw_rounded_rect_aa(rx, ry, rw, rh, 6, 0x001E293B);
            return 0;
        }

        case SYS_DRAW_TEXT: {
            int win_id = (int)a1;
            Window *win = wm_get_window(win_id);
            if (!win || win->is_closed) return (UINT64)-1;

            const char *text = (const char *)a2;
            int tx = win->x + 12 + (int)a3;
            int ty = win->y + 40 + (int)a4;
            wm_draw_text(text, tx, ty, 0x00FFFFFF, 0x00000000);
            return 0;
        }

        case SYS_GET_EVENT: {
            int win_id = (int)a1;
            UserEvent *ev = (UserEvent *)a2;
            if (!ev) return (UINT64)-1;

            Window *win = wm_get_window(win_id);
            if (!win || win->is_closed) {
                ev->type = 0;
                return (UINT64)-1;
            }

            mouse_update();
            MouseState *m = mouse_get_state();
            if (m->left_clicked) {
                if (m->x >= win->x + 12 && m->x <= win->x + win->width - 12 &&
                    m->y >= win->y + 40 && m->y <= win->y + win->height - 12) {
                    ev->type = 1; // Click
                    ev->x = m->x - (win->x + 12);
                    ev->y = m->y - (win->y + 40);
                    return 1;
                }
            }
            return 0;
        }

        case SYS_MARK_DIRTY: {
            wm_mark_all_dirty();
            return 0;
        }
    }
    return (UINT64)-1;
}

/* Syscall ASM Trampoline */
__attribute__((naked)) void syscall_entry_asm(void) {
    __asm__ volatile(
        "pushq %rcx\n\t"               // User RIP
        "pushq %r11\n\t"               // User RFLAGS
        "pushq %rbx\n\t"
        "pushq %rbp\n\t"
        "pushq %r12\n\t"
        "pushq %r13\n\t"
        "pushq %r14\n\t"
        "pushq %r15\n\t"

        "movq %r10, %rcx\n\t"          // 4. Argument
        "call syscall_handler_c\n\t"

        "popq %r15\n\t"
        "popq %r14\n\t"
        "popq %r13\n\t"
        "popq %r12\n\t"
        "popq %rbp\n\t"
        "popq %rbx\n\t"
        "popq %r11\n\t"
        "popq %rcx\n\t"

        "sysretq\n\t"
    );
}

void init_ring3_and_syscalls(void) {
    // 1. MSR EFER: Syscall Extension (SCE) aktivieren
    UINT64 efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | EFER_SCE);

    // 2. MSR STAR: Selektoren
    UINT64 star = ((UINT64)0x00100008ULL << 32);
    wrmsr(MSR_STAR, star);

    // 3. MSR LSTAR: Syscall-Einstiegspunkt
    wrmsr(MSR_LSTAR, (UINT64)syscall_entry_asm);

    // 4. MSR FMASK: RFLAGS Maskierung
    wrmsr(MSR_FMASK, 0x00000700);
}

typedef void (*AppMain)(void);

int load_and_run_app(const char *filename) {
    int port_count = ahci_get_port_count();
    int bytes = -1;

    for (int p = 0; p < port_count; p++) {
        AHCI_PORT_INFO *info = ahci_get_port_info(p);
        if (!info || !info->active) continue;

        bytes = fat32_read_file(info->port_addr, filename, g_app_buffer, sizeof(g_app_buffer));
        if (bytes > 0) break;
    }

    if (bytes <= 0) return 0;

    AppMain app_entry = (AppMain)(void*)g_app_buffer;
    app_entry();
    return 1;
}
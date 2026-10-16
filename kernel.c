#include <stddef.h>
#include <stdint.h>
#include <velo/bootinfo.h>
#include "setup.h"
#include "keyboard.h"
#include "font.h"
#include "ahci.h"
#include "nvme.h"
#include "fat32.h"
#include "desktop.h"
#include "wm.h"
#include "net.h"
#include "syscall.h"
#include "sched.h"
#include "pmm_vmm.h"
#include "kshell.h"
#include "intro.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

UINTN gop_width = 1024;
UINTN gop_height = 768;
UINTN gop_stride = 1024;
VOID* framebuffer_base = NULL;
UINTN framebuffer_size = 0;

UINT32 *g_backbuffer = NULL;
UINT32 *g_desktop_saved_buffer = NULL;

UINT64 g_total_ram_mb = 0;
char g_cpu_brand[49] = "x86_64 Generic Processor";
extern char g_user_name_active[32];
extern char g_pc_name_active[32];

int g_current_tty = 1;
int g_desktop_alive = 1;

static UINT32 g_kernel_backbuffer[1024 * 768] __attribute__((aligned(4096)));
static UINT32 g_kernel_desktop_buf[1024 * 768] __attribute__((aligned(4096)));

typedef struct {
    int  active;
    char user[32];
} KernelSession;

static KernelSession g_sessions[9];

void kernel_session_start(int session_id, const char *username) {
    if (session_id < 1 || session_id > 8) return;
    g_sessions[session_id].active = 1;
    int p = 0;
    while (username && username[p] && p < 31) {
        g_sessions[session_id].user[p] = username[p];
        p++;
    }
    g_sessions[session_id].user[p] = '\0';
}

void kernel_session_end(int session_id) {
    if (session_id < 1 || session_id > 8) return;
    g_sessions[session_id].active = 0;
    g_sessions[session_id].user[0] = '\0';
}

int kernel_session_is_active(int session_id) {
    if (session_id < 1 || session_id > 8) return 0;
    return g_sessions[session_id].active;
}

void mouse_init(void);

/* =========================================================================
 * COMPILER BUILTINS
 * ========================================================================= */
void *memcpy(void *dest, const void *src, size_t n) {
    char *d = (char*)dest;
    const char *s = (const char*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

void *memset(void *s, int c, size_t n) {
    char *p = (char*)s;
    for (size_t i = 0; i < n; i++) p[i] = (char)c;
    return s;
}

void *memmove(void *dest, const void *src, size_t n) {
    char *d = (char*)dest;
    const char *s = (const char*)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char*)s1;
    const unsigned char *p2 = (const unsigned char*)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return (int)p1[i] - (int)p2[i];
    }
    return 0;
}

void put_pixel(UINTN x, UINTN y, UINT32 color) {
    if (!g_backbuffer || x >= gop_width || y >= gop_height) return;
    g_backbuffer[y * gop_width + x] = color;
}

UINT32 get_pixel(UINTN x, UINTN y) {
    if (!g_backbuffer || x >= gop_width || y >= gop_height) return 0;
    return g_backbuffer[y * gop_width + x];
}

void clear_screen_graphics(UINT32 color) {
    if (!g_backbuffer) return;
    UINTN total = gop_width * gop_height;
    for (UINTN i = 0; i < total; i++) g_backbuffer[i] = color;
}

void draw_filled_rect(UINTN start_x, UINTN start_y, UINTN width, UINTN height, UINT32 color) {
    for (UINTN y = start_y; y < start_y + height; y++) {
        for (UINTN x = start_x; x < start_x + width; x++) put_pixel(x, y, color);
    }
}

void swap_buffers_rect(int rx, int ry, int rw, int rh) {
    if (!framebuffer_base || !g_backbuffer || rw <= 0 || rh <= 0) return;

    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > (int)gop_width) rw = (int)gop_width - rx;
    if (ry + rh > (int)gop_height) rh = (int)gop_height - ry;
    if (rw <= 0 || rh <= 0) return;

    UINT32* fb = (UINT32*)framebuffer_base;
    UINTN scanline = gop_stride ? gop_stride : gop_width;
    UINTN copy_bytes = (UINTN)rw * sizeof(UINT32);

    for (int y = ry; y < ry + rh; y++) {
        UINT32* dst = &fb[y * scanline + rx];
        const UINT32* src = &g_backbuffer[y * gop_width + rx];
        memcpy(dst, src, copy_bytes);
    }
}

void swap_buffers(void) {
    swap_buffers_rect(0, 0, (int)gop_width, (int)gop_height);
}

static void enable_sse(void) {
    UINT32 eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    if (!(edx & (1 << 24)) || !(edx & (1 << 25))) return;

    UINT64 cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2); // EM = 0
    cr0 |= (1ULL << 1);  // MP = 1
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9);  // OSFXSR = 1
    cr4 |= (1ULL << 10); // OSXMMEXCPT = 1
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
}

static void detect_cpu_brand_string(void) {
    UINT32 regs[4];
    char *p = g_cpu_brand;
    for (UINT32 leaf = 0x80000002; leaf <= 0x80000004; leaf++) {
        __asm__ volatile("cpuid" : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3]) : "a"(leaf));
        memcpy(p, regs, 16);
        p += 16;
    }
    *p = '\0';
}

static void task_gui_worker(void) {
    static int prev_desktop_alive = 1;
    static int magic_armed = 0;
    static int magic_idx = 0;
    static int magic_timer = 0;
    static const char magic_seq[] = "desktop";

    while (1) {
        unsigned char key = (unsigned char)poll_keyboard_ascii();
        int is_alt = keyboard_is_alt();

        if (is_alt && (key == (unsigned char)KEY_DELETE || key == 0x7F || key == 0x08)) {
            magic_armed = 1;
            magic_idx = 0;
            magic_timer = 5000;
            task_sleep(1);
            continue;
        }

        if (magic_armed) {
            if (magic_timer > 0) magic_timer--;
            else { magic_armed = 0; magic_idx = 0; }
        }

        if (magic_armed && key != 0) {
            if ((key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z')) {
                char lower_k = (key >= 'A' && key <= 'Z') ? (char)(key + 32) : (char)key;
                if (lower_k == magic_seq[magic_idx]) {
                    magic_idx++;
                    magic_timer = 5000;
                    if (magic_idx == 7) {
                        g_desktop_alive = 0;
                        prev_desktop_alive = 0;
                        magic_armed = 0;
                        magic_idx = 0;
                        kernel_session_end(1);
                        for (int w = 0; w < MAX_WINDOWS; w++) {
                            Window *win = wm_get_window(w);
                            if (win && !win->is_closed) wm_close_window(w);
                        }
                        wm_mark_all_dirty();
                        clear_screen_graphics(0x000F172A);
                        swap_buffers();
                        kshell_start(g_current_tty);
                        kshell_mark_dirty();
                        kshell_tick_frame(g_current_tty);
                        task_sleep(1);
                        continue;
                    }
                    task_sleep(1);
                    continue;
                } else {
                    magic_armed = 0;
                    magic_idx = 0;
                }
            }
        }

        if (is_alt && !magic_armed && key != 0) {
            int target_tty = 0;
            if (key >= '1' && key <= '8') target_tty = key - '0';
            if (target_tty >= 1 && target_tty <= 8) {
                if (target_tty != g_current_tty) {
                    int old_tty = g_current_tty;
                    g_current_tty = target_tty;

                    if (old_tty == 1 && g_desktop_alive && g_desktop_saved_buffer && g_backbuffer) {
                        UINTN fb_bytes = gop_width * gop_height * sizeof(UINT32);
                        memcpy(g_desktop_saved_buffer, g_backbuffer, fb_bytes);
                    }

                    if (target_tty == 1 && g_desktop_alive) {
                        if (g_desktop_saved_buffer && g_backbuffer) {
                            UINTN fb_bytes = gop_width * gop_height * sizeof(UINT32);
                            memcpy(g_backbuffer, g_desktop_saved_buffer, fb_bytes);
                            swap_buffers();
                        }
                        wm_mark_all_dirty();
                    } else {
                        kshell_start(g_current_tty);
                        kshell_mark_dirty();
                        kshell_tick_frame(g_current_tty);
                    }
                }
                task_sleep(1);
                continue;
            }
        }

        if (!g_desktop_alive && prev_desktop_alive) {
            prev_desktop_alive = 0;
            kernel_session_end(1);
            clear_screen_graphics(0x000F172A);
            swap_buffers();
            kshell_start(g_current_tty);
            kshell_mark_dirty();
            kshell_tick_frame(g_current_tty);
        } else if (g_desktop_alive) {
            prev_desktop_alive = 1;
        }

        if (g_current_tty == 1 && g_desktop_alive) {
            if (setup_is_active()) {
                if (key != 0) setup_handle_key((char)key);
                setup_tick();
            } else {
                if (key != 0) desktop_handle_key((char)key);
                desktop_tick_frame();
            }
        } else {
            if (key != 0) {
                kshell_handle_key(g_current_tty, (char)key);
            }
            kshell_tick_frame(g_current_tty);
        }

        task_sleep(1);
    }
}

static void task_network_worker(void) {
    while (1) {
        net_poll();
        task_sleep(20);
    }
}

/* =========================================================================
 * KERNEL MAIN ENTRY POINT
 * ========================================================================= */
 void kernel_main(BootInfo *boot_info) {
    if (!boot_info || boot_info->magic != BOOT_MAGIC) {
        while (1) { __asm__ volatile("hlt"); }
    }

    enable_sse();

    framebuffer_base = (VOID*)(UINTN)boot_info->framebuffer_base;
    framebuffer_size = (UINTN)boot_info->framebuffer_size;
    gop_width = (UINTN)boot_info->width;
    gop_height = (UINTN)boot_info->height;
    gop_stride = (UINTN)boot_info->pixels_per_scanline;

    g_backbuffer = g_kernel_backbuffer;
    g_desktop_saved_buffer = g_kernel_desktop_buf;

    g_total_ram_mb = boot_info->total_ram_mb;
    detect_cpu_brand_string();

    pmm_init((void*)(UINTN)boot_info->mmap_addr, 
    (UINTN)boot_info->mmap_size, 
    (UINTN)boot_info->mmap_desc_size,
    (UINT64)boot_info->framebuffer_base,
    (UINT64)boot_info->framebuffer_size);

    init_ring3_and_syscalls();
    init_ahci(0);
    net_init();
    wm_init();
    init_keyboard();
    mouse_init();

    // ==========================================
    // SUPERNATURAL GLITCH INTRO (KALTSTART)
    // ==========================================
    intro_play_glitch_sequence();

    SystemConfig cfg;
    if (load_system_config(&cfg)) {
        setup_disable();
        keyboard_set_layout(cfg.lang);
        int i = 0;
        while (cfg.username[i] && i < 31) {
            g_user_name_active[i] = cfg.username[i];
            i++;
        }
        g_user_name_active[i] = '\0';

        int j = 0;
        while (cfg.pcname[j] && j < 31) {
            g_pc_name_active[j] = cfg.pcname[j];
            j++;
        }
        g_pc_name_active[j] = '\0';

        g_desktop_alive = 1;
        kernel_session_start(1, g_user_name_active);
        desktop_start();
    } else {
        setup_init();
    }

    // Ersten Frame direkt auf den echten Bildschirm zeichnen
    swap_buffers();

    sched_init();
    task_create("GUI_Compositor", task_gui_worker);
    task_create("NetworkServices", task_network_worker);

    sched_start();

    while (1) {
        __asm__ volatile("hlt");
    }
}
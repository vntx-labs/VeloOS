#include <efi.h>
#include <efilib.h>
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

EFI_SYSTEM_TABLE* g_st = NULL;
EFI_HANDLE g_image_handle = NULL;
EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;

UINTN gop_width = 1024;
UINTN gop_height = 768;
VOID* framebuffer_base = NULL;
UINTN framebuffer_size = 0;

UINT32 *g_backbuffer = NULL;
UINT32 *g_wall_buffer = NULL;

UINT64 g_total_ram_mb = 0;
char g_cpu_brand[49] = "Unbekannter Prozessor";
extern char g_user_name_active[32];
extern char g_pc_name_active[32];

/* Globale Statusvariablen */
int g_current_tty = 1;     // TTY1 = Desktop, TTY2..8 = Notfall-Shells
int g_desktop_alive = 1;   // 1 = Desktop aktiv, 0 = Gekillt

void swap_buffers_rect(int rx, int ry, int rw, int rh);
void swap_buffers(void);

void klog(const char *s) { (void)s; }

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
    if (!framebuffer_base || !gop || !g_backbuffer || rw <= 0 || rh <= 0) return;

    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > (int)gop_width) rw = (int)gop_width - rx;
    if (ry + rh > (int)gop_height) rh = (int)gop_height - ry;
    if (rw <= 0 || rh <= 0) return;

    UINT32* fb = (UINT32*)framebuffer_base;
    UINTN scanline = (gop->Mode && gop->Mode->Info) ? gop->Mode->Info->PixelsPerScanLine : gop_width;
    if (scanline == 0) scanline = gop_width;

    UINTN copy_bytes = (UINTN)rw * sizeof(UINT32);

    for (int y = ry; y < ry + rh; y++) {
        UINT32* dst = &fb[y * scanline + rx];
        const UINT32* src = &g_backbuffer[y * gop_width + rx];
        __builtin_memcpy(dst, (const void*)src, copy_bytes);
    }
}

void swap_buffers(void) {
    swap_buffers_rect(0, 0, (int)gop_width, (int)gop_height);
}

void *memmove(void *dest, const void *src, UINTN n) {
    UINT8 *d = (UINT8*)dest;
    const UINT8 *s = (const UINT8*)src;
    if (d < s) {
        for (UINTN i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        for (UINTN i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dest;
}

static void enable_sse(void) {
    UINT64 cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2); cr0 &= ~(1ULL << 3); cr0 |= (1ULL << 1);
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9); cr4 |= (1ULL << 10);
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
}

static void detect_cpu_brand_string(void) {
    UINT32 regs[4];
    char *p = g_cpu_brand;
    for (UINT32 leaf = 0x80000002; leaf <= 0x80000004; leaf++) {
        __asm__ volatile("cpuid" : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3]) : "a"(leaf));
        __builtin_memcpy(p, regs, 16);
        p += 16;
    }
    *p = '\0';

    char *start = g_cpu_brand;
    while (*start == ' ') start++;
    if (start != g_cpu_brand) {
        int i = 0;
        while (start[i]) { g_cpu_brand[i] = start[i]; i++; }
        g_cpu_brand[i] = '\0';
    }
}

static UINT64 detect_real_ram_from_mmap(void) {
    UINTN map_size = 0, map_key = 0, desc_size = 0;
    UINT32 desc_ver = 0;
    uefi_call_wrapper(BS->GetMemoryMap, 5, &map_size, NULL, &map_key, &desc_size, &desc_ver);
    map_size += 4096;
    UINTN pages = EFI_SIZE_TO_PAGES(map_size);
    EFI_PHYSICAL_ADDRESS phys = 0;

    if (EFI_ERROR(uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, pages, &phys))) return 4096;
    EFI_MEMORY_DESCRIPTOR *mmap = (EFI_MEMORY_DESCRIPTOR*)(UINTN)phys;
    UINTN cur_size = pages * 4096;
    if (EFI_ERROR(uefi_call_wrapper(BS->GetMemoryMap, 5, &cur_size, mmap, &map_key, &desc_size, &desc_ver))) return 4096;

    if (desc_size == 0) return 4096;
    pmm_init(mmap, cur_size, desc_size);

    UINT64 total_bytes = 0;
    UINTN num_entries = cur_size / desc_size;
    for (UINTN i = 0; i < num_entries; i++) {
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)mmap + i * desc_size);
        if (desc->Type == EfiConventionalMemory || 
            desc->Type == EfiLoaderCode || desc->Type == EfiLoaderData ||
            desc->Type == EfiBootServicesCode || desc->Type == EfiBootServicesData) {
            total_bytes += desc->NumberOfPages * 4096ULL;
        }
    }
    return total_bytes / (1024 * 1024);
}

void init_gop(void) {
    EFI_STATUS status;
    status = LibLocateProtocol(&GraphicsOutputProtocol, (VOID**)&gop);
    if (EFI_ERROR(status) || !gop) {
        EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
        status = uefi_call_wrapper(BS->LocateProtocol, 3, &gop_guid, NULL, (VOID**)&gop);
    }
    if (EFI_ERROR(status) || !gop || !gop->Mode || !gop->Mode->Info) return;

    UINT32 best_mode = gop->Mode->Mode;
    UINTN best_w = 0, size_of_info = 0;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;

    for (UINT32 m = 0; m < gop->Mode->MaxMode; m++) {
        status = uefi_call_wrapper(gop->QueryMode, 4, gop, m, &size_of_info, &info);
        if (EFI_ERROR(status) || !info) continue;

        if (info->HorizontalResolution == 1024 && info->VerticalResolution == 768) {
            best_mode = m;
            break;
        }
        if (info->HorizontalResolution >= 800 && info->HorizontalResolution > best_w) {
            best_mode = m;
            best_w = info->HorizontalResolution;
        }
    }

    if (best_mode != gop->Mode->Mode) {
        uefi_call_wrapper(gop->SetMode, 2, gop, best_mode);
    }

    if (ST && ST->ConOut) {
        uefi_call_wrapper(ST->ConOut->EnableCursor, 2, ST->ConOut, FALSE);
    }

    gop_width = gop->Mode->Info->HorizontalResolution;
    gop_height = gop->Mode->Info->VerticalResolution;
    framebuffer_base = (VOID*)(UINTN)gop->Mode->FrameBufferBase;
    framebuffer_size = gop->Mode->FrameBufferSize;

    UINTN fb_bytes = gop_width * gop_height * sizeof(UINT32);
    if (fb_bytes < 1920 * 1080 * sizeof(UINT32)) fb_bytes = 1920 * 1080 * sizeof(UINT32);

    g_backbuffer = (UINT32*)AllocateZeroPool(fb_bytes);
    if (!g_backbuffer) {
        UINTN fb_pages = EFI_SIZE_TO_PAGES(fb_bytes);
        EFI_PHYSICAL_ADDRESS fb_phys = 0;
        status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, fb_pages, &fb_phys);
        if (status == EFI_SUCCESS && fb_phys != 0) {
            g_backbuffer = (UINT32*)(UINTN)fb_phys;
            for (UINTN i = 0; i < fb_bytes / 4; i++) g_backbuffer[i] = 0;
        }
    }
}

// =========================================================================
// KERNEL COMPOSITOR & TTY CONTROLLER (Strg + Alt + 1..8)
// =========================================================================
static void task_gui_worker(void) {
    static int prev_desktop_alive = 1;

    while (1) {
        char key = poll_keyboard_ascii();

        // 1. TTY-WECHSEL: Alt + 1..8 (oder Strg + Alt + 1..8)
        if (keyboard_is_alt()) {
            if (key >= '1' && key <= '8') {
                int target_tty = key - '0';
                if (target_tty != g_current_tty) {
                    g_current_tty = target_tty;
                    if (g_current_tty == 1 && g_desktop_alive) {
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

        // 2. KERNEL-SUPERVISOR: Desktop beendet -> EINMALIG initialisieren & zeichnen!
        if (!g_desktop_alive && prev_desktop_alive) {
            prev_desktop_alive = 0;
            kshell_start(g_current_tty);
            kshell_mark_dirty();
            kshell_tick_frame(g_current_tty);
        } else if (g_desktop_alive) {
            prev_desktop_alive = 1;
        }

        // 3. Frame-Dispatching
        if (g_current_tty == 1 && g_desktop_alive) {
            if (setup_is_active()) {
                if (key != 0) setup_handle_key(key);
                setup_tick();
            } else {
                if (key != 0) desktop_handle_key(key);
                desktop_tick_frame();
            }
        } else {
            // TTY 2..8 ODER TTY1 im Notfallmodus:
            if (key != 0) {
                kshell_handle_key(g_current_tty, key);
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

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    g_st = SystemTable;
    g_image_handle = ImageHandle;
    InitializeLib(ImageHandle, SystemTable);

    enable_sse();
    init_ring3_and_syscalls();

    if (BS && BS->SetWatchdogTimer) {
        uefi_call_wrapper(BS->SetWatchdogTimer, 4, 0, 0, 0, NULL);
    }

    g_total_ram_mb = detect_real_ram_from_mmap();
    detect_cpu_brand_string();
    init_gop();
    init_ahci(0);
    net_init();
    wm_init();

    sched_init();
    task_create("GUI_Compositor", task_gui_worker);
    task_create("NetworkServices", task_network_worker);

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
        desktop_start();
    } else {
        setup_init();
    }

    sched_start();

    while (1) {
        __asm__ volatile("hlt");
    }

    return EFI_SUCCESS;
}
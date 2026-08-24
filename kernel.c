#include <efi.h>
#include <efilib.h>
#include "keyboard.h"
#include "font.h"
#include "ahci.h"
#include "fat32.h"
#include "desktop.h"
#include "wm.h"
#include "setup.h"
#include "syscall.h"

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

    if (EFI_ERROR(uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, pages, &phys))) return 0;
    EFI_MEMORY_DESCRIPTOR *mmap = (EFI_MEMORY_DESCRIPTOR*)(UINTN)phys;
    UINTN cur_size = pages * 4096;
    if (EFI_ERROR(uefi_call_wrapper(BS->GetMemoryMap, 5, &cur_size, mmap, &map_key, &desc_size, &desc_ver))) return 0;

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

static void enable_cpu_features(void) {
    UINT64 cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 3);
    cr0 &= ~(1ULL << 2);
    cr0 |= (1ULL << 1);
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9);
    cr4 |= (1ULL << 10);
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
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
    if (!framebuffer_base || !gop || !g_backbuffer || rw <= 0 || rh <= 0) return;

    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > (int)gop_width) rw = (int)gop_width - rx;
    if (ry + rh > (int)gop_height) rh = (int)gop_height - ry;
    if (rw <= 0 || rh <= 0) return;

    UINT32* fb = (UINT32*)framebuffer_base;
    UINTN scanline = (gop->Mode && gop->Mode->Info) ? gop->Mode->Info->PixelsPerScanLine : gop_width;
    if (scanline == 0) scanline = gop_width;

    for (int y = ry; y < ry + rh; y++) {
        UINT32* dst = &fb[y * scanline + rx];
        const UINT32* src = &g_backbuffer[y * gop_width + rx];
        for (int x = 0; x < rw; x++) {
            dst[x] = src[x];
        }
    }
}

void swap_buffers(void) {
    swap_buffers_rect(0, 0, (int)gop_width, (int)gop_height);
}

static inline void outw(unsigned short port, unsigned short val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline void outl(unsigned short port, unsigned int val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

void system_shutdown(void) {
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    if (g_st && g_st->RuntimeServices) {
        g_st->RuntimeServices->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
    }
    while(1) { __asm__ volatile("cli; hlt"); }
}

void system_reboot(void) {
    if (g_st && g_st->RuntimeServices) {
        g_st->RuntimeServices->ResetSystem(EfiResetWarm, EFI_SUCCESS, 0, NULL);
    }
    outl(0xCF8, 0x8000F838);
    while(1) { __asm__ volatile("cli; hlt"); }
}

void init_gop(void) {
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_STATUS status = uefi_call_wrapper(BS->LocateProtocol, 3, &gop_guid, NULL, (VOID**)&gop);
    if (status != EFI_SUCCESS || gop == NULL || !gop->Mode || !gop->Mode->Info) return;

    gop_width = gop->Mode->Info->HorizontalResolution;
    gop_height = gop->Mode->Info->VerticalResolution;
    framebuffer_base = (VOID*)gop->Mode->FrameBufferBase;
    framebuffer_size = gop->Mode->FrameBufferSize;

    UINTN fb_bytes = gop_width * gop_height * sizeof(UINT32);
    if (fb_bytes < 1920 * 1080 * sizeof(UINT32)) fb_bytes = 1920 * 1080 * sizeof(UINT32);
    UINTN fb_pages = EFI_SIZE_TO_PAGES(fb_bytes);

    EFI_PHYSICAL_ADDRESS fb_phys = 0;
    status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, fb_pages, &fb_phys);
    if (status == EFI_SUCCESS && fb_phys != 0) {
        g_backbuffer = (UINT32*)(UINTN)fb_phys;
        for (UINTN i = 0; i < fb_bytes / 4; i++) g_backbuffer[i] = 0;
    }
}

static char get_key_stroke(void) {
    if (!ST || !ST->ConIn) return 0;
    EFI_INPUT_KEY key;
    EFI_STATUS status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &key);
    if (status == EFI_SUCCESS) {
        if (key.ScanCode == 0x0B) return KEY_F1;
        if (key.ScanCode == 0x17) return KEY_ESC;

        if (key.UnicodeChar >= 32) {
            return (char)key.UnicodeChar;
        }
        if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') return '\n';
        if (key.UnicodeChar == '\b') return '\b';
        if (key.UnicodeChar == '\t') return '\t';
    }
    return 0;
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    g_st = SystemTable;
    g_image_handle = ImageHandle;
    InitializeLib(ImageHandle, SystemTable);

    enable_cpu_features();
    init_ring3_and_syscalls();

    if (BS && BS->SetWatchdogTimer) {
        uefi_call_wrapper(BS->SetWatchdogTimer, 4, 0, 0, 0, NULL);
    }

    g_total_ram_mb = detect_real_ram_from_mmap();
    detect_cpu_brand_string();
    init_gop();
    init_ahci(0);

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
        desktop_start();
    } else {
        setup_init();
    }

    while (1) {
        char key = get_key_stroke();

        if (setup_is_active()) {
            if (key != 0) {
                setup_handle_key(key);
            }
            setup_tick();
        } else {
            if (key != 0) {
                desktop_handle_key(key);
            }
            desktop_tick_frame();
        }

        if (BS && BS->Stall) {
            uefi_call_wrapper(BS->Stall, 1, 10000);
        }
    }

    return EFI_SUCCESS;
}
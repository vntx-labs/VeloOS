// kernel.c - UEFI OS Kernel mit dynamischer On-the-Fly Auflösungserkennung
#include <efi.h>
#include <efilib.h>
#include "keyboard.h"
#include "font.h"
#include "ahci.h"
#include "fat32.h"
#include "desktop.h"
#include "wm.h"

#define MAX_DRIVES 8

char command_buffer[64];
int command_length = 0;
EFI_SYSTEM_TABLE* g_st;
EFI_HANDLE g_image_handle;

typedef struct {
    void *port;
    int port_number;
    UINT64 size_mb;
    UINT64 sector_count;
    int has_fat32;
    char label[32];
} DriveInfo;

static DriveInfo drives[MAX_DRIVES];
static int num_drives = 0;
static int selected_drive = -1;
static int drive_select_mode = 0;

EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
UINTN gop_width = 0;
UINTN gop_height = 0;
VOID* framebuffer_base = NULL;
UINTN framebuffer_size = 0;

/* Globale Puffer für Backbuffer und Wallpaper */
UINT32 *g_backbuffer = NULL;
UINT32 *g_wall_buffer = NULL;

UINTN gfx_cursor_x = 50;
UINTN gfx_cursor_y = 190;

int is_bare_metal = 0;
int system_mode = 0;
int logged_in = 0;

int format_mode = 0;
char format_confirm[10];
int format_confirm_len = 0;

char login_username[32];
char login_password[32];
int login_field_focus = 0;
int login_len_u = 0;
int login_len_p = 0;
int login_error = 0;

char ui_username[32];
char ui_password[32];
int ui_field_focus = 0;
int ui_input_len_u = 0;
int ui_input_len_p = 0;

typedef struct {
    UINTN MapSize;
    EFI_MEMORY_DESCRIPTOR *Map;
    UINTN MapKey;
    UINTN DescriptorSize;
    UINT32 DescriptorVersion;
} MemoryMap;

MemoryMap mmap;

void put_pixel(UINTN x, UINTN y, UINT32 color);
UINT32 get_pixel(UINTN x, UINTN y);
void clear_screen_graphics(UINT32 color);
void draw_string(const char* str, UINTN x, UINTN y, UINT32 fg_color, UINT32 bg_color);
void draw_filled_rect(UINTN start_x, UINTN start_y, UINTN width, UINTN height, UINT32 color);
void draw_window(UINTN x, UINTN y, UINTN w, UINTN h, const char* title);
void swap_buffers(void);
void swap_buffers_rect(int rx, int ry, int rw, int rh);

void show_login_screen(void);
void show_account_creator_ui(void);
void show_drive_select_dialog(void);
void show_format_dialog(void);

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

void encrypt_data(char *data, UINT32 size) {
    char key = 0x5A;
    for (UINT32 i = 0; i < size; i++) {
        data[i] ^= key;
    }
}

int check_account_exists(void) {
    if (selected_drive < 0 || selected_drive >= num_drives) return 0;
    void *port = drives[selected_drive].port;
    if (!port) return 0;
    char record[64];
    __builtin_memset(record, 0, sizeof(record));
    int result = fat32_read_file(port, "ACCOUNT.DAT", record, sizeof(record));
    __builtin_memset(record, 0, sizeof(record));
    return result == 64;
}

int save_account_to_disk(const char *user, const char *pass) {
    if (selected_drive < 0 || selected_drive >= num_drives) return 0;
    void *port = drives[selected_drive].port;
    if (!port || !user || !pass) return 0;

    char record[64];
    char verify[64];
    __builtin_memset(record, 0, sizeof(record));
    __builtin_memset(verify, 0, sizeof(verify));

    int i = 0;
    while (user[i] && i < 31) {
        record[i] = user[i];
        i++;
    }

    i = 32;
    int j = 0;
    while (pass[j] && j < 31) {
        record[i++] = pass[j++];
    }

    encrypt_data(record, sizeof(record));

    if (!fat32_write_file(port, "ACCOUNT.DAT", record, sizeof(record))) {
        __builtin_memset(record, 0, sizeof(record));
        __builtin_memset(verify, 0, sizeof(verify));
        return 0;
    }

    int verify_result = fat32_read_file(port, "ACCOUNT.DAT", verify, sizeof(verify));
    int identical = (verify_result == 64);

    if (identical) {
        for (int k = 0; k < 64; k++) {
            if ((unsigned char)record[k] != (unsigned char)verify[k]) {
                identical = 0;
                break;
            }
        }
    }

    __builtin_memset(record, 0, sizeof(record));
    __builtin_memset(verify, 0, sizeof(verify));
    return identical;
}

int verify_login(const char *user, const char *pass) {
    if (selected_drive < 0 || selected_drive >= num_drives) return 0;
    void *port = drives[selected_drive].port;
    if (!port) return 0;

    char record[64];
    __builtin_memset(record, 0, 64);

    if (fat32_read_file(port, "ACCOUNT.DAT", record, 64) != 64) {
        return 0;
    }

    encrypt_data(record, 64);

    char disk_user[32];
    char disk_pass[32];
    __builtin_memset(disk_user, 0, 32);
    __builtin_memset(disk_pass, 0, 32);

    for (int i = 0; i < 32; i++) disk_user[i] = record[i];
    for (int i = 0; i < 32; i++) disk_pass[i] = record[32 + i];

    int match = (strcmp(disk_user, user) == 0 && strcmp(disk_pass, pass) == 0);

    __builtin_memset(record, 0, 64);
    __builtin_memset(disk_pass, 0, 32);

    return match;
}

static inline void outl(unsigned short port, unsigned int val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline void outw(unsigned short port, unsigned short val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline unsigned int inl(unsigned short port) {
    unsigned int ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
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

int format_disk_fat32(void *port) {
    if (!port || selected_drive < 0 || selected_drive >= num_drives) return 0;
    UINT64 sec_count = drives[selected_drive].sector_count;
    if (sec_count == 0) {
        sec_count = drives[selected_drive].size_mb * 2048ULL;
    }
    return fat32_format(port, sec_count);
}

/*
 * Initialisiert GOP und reserviert Speicherseiten sicher
 */
void init_gop(void) {
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_STATUS status = g_st->BootServices->LocateProtocol(&gop_guid, NULL, (VOID**)&gop);
    if (status != EFI_SUCCESS || gop == NULL || !gop->Mode || !gop->Mode->Info) return;

    gop_width = gop->Mode->Info->HorizontalResolution;
    gop_height = gop->Mode->Info->VerticalResolution;
    framebuffer_base = (VOID*)gop->Mode->FrameBufferBase;
    framebuffer_size = gop->Mode->FrameBufferSize;

    /* Puffergröße auf mind. 1920x1080 (8.3 MB) festlegen */
    UINTN fb_bytes = gop_width * gop_height * sizeof(UINT32);
    if (fb_bytes < 1920 * 1080 * sizeof(UINT32)) {
        fb_bytes = 1920 * 1080 * sizeof(UINT32);
    }
    UINTN fb_pages = EFI_SIZE_TO_PAGES(fb_bytes);

    /* 1. Backbuffer Seiten allozieren */
    EFI_PHYSICAL_ADDRESS fb_phys = 0;
    status = g_st->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, fb_pages, &fb_phys);
    if (status == EFI_SUCCESS && fb_phys != 0) {
        g_backbuffer = (UINT32*)(UINTN)fb_phys;
        for (UINTN i = 0; i < fb_bytes / 4; i++) g_backbuffer[i] = 0;
    }

    /* 2. Wallpaper Puffer Seiten allozieren (1920x1080 * 4 = 8.3 MB) */
    UINTN wall_bytes = 1920 * 1080 * sizeof(UINT32) + 4096;
    UINTN wall_pages = EFI_SIZE_TO_PAGES(wall_bytes);

    EFI_PHYSICAL_ADDRESS wall_phys = 0;
    status = g_st->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, wall_pages, &wall_phys);
    if (status == EFI_SUCCESS && wall_phys != 0) {
        g_wall_buffer = (UINT32*)(UINTN)wall_phys;
        for (UINTN i = 0; i < wall_bytes / 4; i++) g_wall_buffer[i] = 0;
    }
}

/*
 * Erkennt Auflösungs- & Framebuffer-Änderungen "On the Fly"
 */
void check_screen_resolution_change(void) {
    if (!gop || !gop->Mode || !gop->Mode->Info) return;

    UINTN current_w = gop->Mode->Info->HorizontalResolution;
    UINTN current_h = gop->Mode->Info->VerticalResolution;
    VOID* current_fb = (VOID*)gop->Mode->FrameBufferBase;

    if (current_w != gop_width || current_h != gop_height || current_fb != framebuffer_base) {
        gop_width = current_w;
        gop_height = current_h;
        framebuffer_base = current_fb;
        framebuffer_size = gop->Mode->FrameBufferSize;

        wm_mark_all_dirty();
    }
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
    for (UINTN i = 0; i < total; i++) {
        g_backbuffer[i] = color;
    }
}

/* 128-Bit SSE2 Hardware Blitter */
void swap_buffers_rect(int rx, int ry, int rw, int rh) {
    if (!framebuffer_base || !gop || !g_backbuffer) return;
    if (rw <= 0 || rh <= 0) return;

    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > (int)gop_width) rw = (int)gop_width - rx;
    if (ry + rh > (int)gop_height) rh = (int)gop_height - ry;
    if (rw <= 0 || rh <= 0) return;

    UINT32* fb = (UINT32*)framebuffer_base;
    UINTN scanline = (gop->Mode && gop->Mode->Info) ? gop->Mode->Info->PixelsPerScanLine : gop_width;
    if (scanline == 0) scanline = gop_width;

    UINTN copy_bytes = (UINTN)rw * sizeof(UINT32);
    UINTN blocks_64 = copy_bytes / 64;
    UINTN rem_bytes = copy_bytes % 64;

    for (int y = ry; y < ry + rh; y++) {
        UINT8* dst_ptr = (UINT8*)&fb[y * scanline + rx];
        const UINT8* src_ptr = (const UINT8*)&g_backbuffer[y * gop_width + rx];

        for (UINTN b = 0; b < blocks_64; b++) {
            __asm__ volatile(
                "movdqu 0(%1), %%xmm0\n\t"
                "movdqu 16(%1), %%xmm1\n\t"
                "movdqu 32(%1), %%xmm2\n\t"
                "movdqu 48(%1), %%xmm3\n\t"
                "movdqu %%xmm0, 0(%0)\n\t"
                "movdqu %%xmm1, 16(%0)\n\t"
                "movdqu %%xmm2, 32(%0)\n\t"
                "movdqu %%xmm3, 48(%0)\n\t"
                :
                : "r"(dst_ptr + b * 64), "r"(src_ptr + b * 64)
                : "xmm0", "xmm1", "xmm2", "xmm3", "memory"
            );
        }

        if (rem_bytes > 0) {
            UINTN offset = blocks_64 * 64;
            UINT64* d64 = (UINT64*)(dst_ptr + offset);
            const UINT64* s64 = (const UINT64*)(src_ptr + offset);
            UINTN qwords = rem_bytes / 8;
            for (UINTN q = 0; q < qwords; q++) d64[q] = s64[q];
        }
    }
}

void swap_buffers(void) {
    swap_buffers_rect(0, 0, (int)gop_width, (int)gop_height);
}

void draw_char(char c, UINTN x, UINTN y, UINT32 fg_color, UINT32 bg_color) {
    const unsigned char* glyph = font8x16[(unsigned char)c];
    int scale = 2;
    for (int cy = 0; cy < 16; cy++) {
        unsigned char line = glyph[cy];
        for (int cx = 0; cx < 8; cx++) {
            UINT32 color = (line & (1 << (7 - cx))) ? fg_color : bg_color;
            for (int sy = 0; sy < scale; sy++) {
                for (int sx = 0; sx < scale; sx++) {
                    put_pixel(x + (cx * scale) + sx, y + (cy * scale) + sy, color);
                }
            }
        }
    }
}

void draw_string(const char* str, UINTN x, UINTN y, UINT32 fg_color, UINT32 bg_color) {
    UINTN cx = x; UINTN cy = y;
    int scale = 2;
    int char_width = 8 * scale;   
    int char_height = 16 * scale; 
    while (*str) {
        if (*str == '\n') { cy += char_height + 4; cx = x; }
        else { draw_char(*str, cx, cy, fg_color, bg_color); cx += char_width; }
        str++;
    }
}

void draw_filled_rect(UINTN start_x, UINTN start_y, UINTN width, UINTN height, UINT32 color) {
    for (UINTN y = start_y; y < start_y + height; y++) {
        for (UINTN x = start_x; x < start_x + width; x++) put_pixel(x, y, color);
    }
}

void draw_window(UINTN x, UINTN y, UINTN w, UINTN h, const char* title) {
    draw_rounded_rect_aa((int)x + 6, (int)y + 6, (int)w, (int)h, 10, 0x00060810);
    draw_rounded_rect_aa((int)x, (int)y, (int)w, (int)h, 8, 0x00181A24);
    draw_rounded_rect_gradient((int)x, (int)y, (int)w, 34, 8, 0x001B62D6, 0x000E429C);
    draw_filled_rect(x, y + 24, w, 10, 0x000E429C);
    draw_string(title, x + 12, y + 8, 0x00FFFFFF, 0x000E429C);
    draw_rounded_rect_aa((int)(x + w - 30), (int)y + 4, 24, 24, 6, 0x00E06C75);
    draw_string("X", x + w - 22, y + 8, 0x00FFFFFF, 0x00E06C75);
}

void show_drive_select_dialog(void) {
    clear_screen_graphics(0x000A0C14);
    
    UINTN win_w = 700;
    UINTN win_h = 400;
    if (win_w > gop_width - 40) win_w = gop_width - 40;
    if (win_h > gop_height - 40) win_h = gop_height - 40;

    UINTN win_x = (gop_width - win_w) / 2;
    UINTN win_y = (gop_height - win_h) / 2;
    
    draw_window(win_x, win_y, win_w, win_h, "LAUFWERKS-AUSWAHL");
    draw_string("Mehrere Laufwerke erkannt. Bitte Speichermedium waehlen:", win_x + 20, win_y + 50, 0x00FFFFFF, 0x00181A24);
    
    int y = win_y + 100;
    for (int i = 0; i < num_drives && i < MAX_DRIVES; i++) {
        UINT32 bg_color = (i == selected_drive) ? 0x001B62D6 : 0x0024283B;
        draw_rounded_rect_aa((int)(win_x + 20), y, (int)(win_w - 40), 40, 6, bg_color);
        
        char info[64];
        int pos = 0;
        info[pos++] = 'P'; info[pos++] = 'o'; info[pos++] = 'r'; info[pos++] = 't'; info[pos++] = ' ';
        info[pos++] = '0' + (char)drives[i].port_number;
        info[pos++] = ':'; info[pos++] = ' ';
        
        if (drives[i].size_mb >= 1000) {
            info[pos++] = '0' + (char)(drives[i].size_mb / 1000);
            info[pos++] = 'G'; info[pos++] = 'B';
        } else {
            if (drives[i].size_mb >= 100) info[pos++] = '0' + (char)(drives[i].size_mb / 100);
            if (drives[i].size_mb >= 10) info[pos++] = '0' + (char)((drives[i].size_mb / 10) % 10);
            info[pos++] = '0' + (char)(drives[i].size_mb % 10);
            info[pos++] = 'M'; info[pos++] = 'B';
        }
        info[pos++] = ' '; info[pos++] = '-'; info[pos++] = ' ';
        
        if (drives[i].has_fat32) {
            info[pos++] = 'F'; info[pos++] = 'A'; info[pos++] = 'T'; info[pos++] = '3'; info[pos++] = '2';
            info[pos++] = ' '; info[pos++] = 'O'; info[pos++] = 'K';
        } else {
            info[pos++] = 'N'; info[pos++] = 'I'; info[pos++] = 'C'; info[pos++] = 'H'; info[pos++] = 'T';
            info[pos++] = ' '; info[pos++] = 'F'; info[pos++] = 'O'; info[pos++] = 'R'; info[pos++] = 'M';
            info[pos++] = 'A'; info[pos++] = 'T'; info[pos++] = 'I'; info[pos++] = 'E'; info[pos++] = 'R'; info[pos++] = 'T';
        }
        info[pos] = '\0';
        
        draw_string(info, win_x + 30, y + 12, (i == selected_drive) ? 0x0000FFCC : 0x00FFFFFF, bg_color);
        y += 50;
    }
    
    draw_string("[W / S] Auswaehlen | [ENTER] Bestaetigen", win_x + 20, win_y + win_h - 40, 0x00777788, 0x00181A24);
    swap_buffers();
}

void show_format_dialog(void) {
    clear_screen_graphics(0x000A0C14);
    
    UINTN win_w = 600;
    UINTN win_h = 400;
    if (win_w > gop_width - 40) win_w = gop_width - 40;
    if (win_h > gop_height - 40) win_h = gop_height - 40;

    UINTN win_x = (gop_width - win_w) / 2;
    UINTN win_y = (gop_height - win_h) / 2;
    
    draw_window(win_x, win_y, win_w, win_h, "FORMATIERUNG ERFORDERLICH");
    draw_string("Das gewaehlte Laufwerk ist nicht mit FAT32 formatiert.", win_x + 20, win_y + 50, 0x00FFFFFF, 0x00181A24);
    
    if (selected_drive >= 0 && selected_drive < num_drives) {
        draw_string("Laufwerk: Port ", win_x + 20, win_y + 90, 0x00FFFF00, 0x00181A24);
        char port_num[2] = {'0' + (char)drives[selected_drive].port_number, '\0'};
        draw_string(port_num, win_x + 180, win_y + 90, 0x00FFFF00, 0x00181A24);
        
        draw_string("Schaetzung Groesse:", win_x + 20, win_y + 120, 0x00FFFF00, 0x00181A24);
        char size_str[16];
        int pos = 0;
        UINT64 mb = drives[selected_drive].size_mb;
        if (mb >= 1000) {
            size_str[pos++] = '0' + (char)(mb / 1000);
            size_str[pos++] = 'G'; size_str[pos++] = 'B';
        } else {
            if (mb >= 100) size_str[pos++] = '0' + (char)(mb / 100);
            if (mb >= 10) size_str[pos++] = '0' + (char)((mb / 10) % 10);
            size_str[pos++] = '0' + (char)(mb % 10);
            size_str[pos++] = 'M'; size_str[pos++] = 'B';
        }
        size_str[pos] = '\0';
        draw_string(size_str, win_x + 350, win_y + 120, 0x0000FFCC, 0x00181A24);
    }
    
    draw_string("!!! WARNUNG !!!", win_x + 20, win_y + 170, 0x00FF0000, 0x00181A24);
    draw_string("Diese Operation wird ALLE DATEN auf dem Laufwerk", win_x + 20, win_y + 200, 0x00FF0000, 0x00181A24);
    draw_string("unwiederruflich LOESCHEN!", win_x + 20, win_y + 230, 0x00FF0000, 0x00181A24);
    
    draw_string("Geben Sie 'YES' ein, um fortzufahren:", win_x + 20, win_y + 280, 0x00FFFFFF, 0x00181A24);
    draw_rounded_rect_aa((int)(win_x + 20), (int)(win_y + 310), 200, 30, 6, 0x0024283B);
    draw_string(format_confirm, win_x + 30, win_y + 316, 0x0000FF00, 0x0024283B);
    
    draw_string("[ESC] Abbrechen", win_x + 20, win_y + win_h - 40, 0x00777788, 0x00181A24);
    swap_buffers();
}

void handle_drive_select_key(char c) {
    if (c == 0x1B) {
        drive_select_mode = 0;
        system_mode = 0;
        clear_screen_graphics(0x00000000);
        draw_string("VeloOS Kernel - Bare Metal Mode Active", 50, 30, 0x00FFFFFF, 0x00000000);
        draw_string("Kein Laufwerk ausgewaehlt.", 50, 60, 0x00FFAA00, 0x00000000);
        gfx_cursor_x = 50; gfx_cursor_y = 120;
        draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
        gfx_cursor_x += 32;
        swap_buffers();
        return;
    }
    
    if (c == '\n' || c == '\r') {
        if (selected_drive >= 0 && selected_drive < num_drives) {
            drive_select_mode = 0;
            
            if (drives[selected_drive].has_fat32) {
                clear_screen_graphics(0x00000000);
                draw_string("VeloOS Kernel - Bare Metal Mode Active", 50, 30, 0x00FFFFFF, 0x00000000);
                draw_string("Laufwerk bereit.", 50, 60, 0x0000FFCC, 0x00000000);
                gfx_cursor_x = 50; gfx_cursor_y = 120;
                draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
                gfx_cursor_x += 32;
                swap_buffers();
            } else {
                format_mode = 1;
                format_confirm_len = 0;
                format_confirm[0] = '\0';
                show_format_dialog();
            }
        }
        return;
    }
    
    if (c == 'w' || c == 'W') {
        selected_drive--;
        if (selected_drive < 0) selected_drive = num_drives - 1;
    } else if (c == 's' || c == 'S') {
        selected_drive++;
        if (selected_drive >= num_drives) selected_drive = 0;
    }
    
    show_drive_select_dialog();
}

void handle_format_key(char c) {
    if (c == 0x1B) {
        format_mode = 0;
        selected_drive = -1;
        system_mode = 0;
        clear_screen_graphics(0x00000000);
        draw_string("VeloOS Kernel - Bare Metal Mode Active", 50, 30, 0x00FFFFFF, 0x00000000);
        draw_string("Formatierung abgebrochen.", 50, 60, 0x00FF0000, 0x00000000);
        gfx_cursor_x = 50; gfx_cursor_y = 120;
        draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
        gfx_cursor_x += 32;
        swap_buffers();
        return;
    }
    
    if (c == '\n' || c == '\r') {
        if (strcmp(format_confirm, "YES") == 0) {
            if (selected_drive >= 0 && selected_drive < num_drives) {
                clear_screen_graphics(0x00000000);
                draw_string("Formatiere Laufwerk mit FAT32...", 50, 100, 0x00FFFFFF, 0x00000000);
                swap_buffers();
                
                if (format_disk_fat32(drives[selected_drive].port)) {
                    drives[selected_drive].has_fat32 = 1;
                    format_mode = 0;
                    clear_screen_graphics(0x00000000);
                    draw_string("VeloOS Kernel - Bare Metal Mode Active", 50, 30, 0x00FFFFFF, 0x00000000);
                    draw_string("Laufwerk erfolgreich formatiert.", 50, 60, 0x0000FFCC, 0x00000000);
                } else {
                    selected_drive = -1;
                    format_mode = 0;
                    clear_screen_graphics(0x00000000);
                    draw_string("VeloOS Kernel - Bare Metal Mode Active", 50, 30, 0x00FFFFFF, 0x00000000);
                    draw_string("FEHLER: Formatierung fehlgeschlagen!", 50, 60, 0x00FF0000, 0x00000000);
                }
                gfx_cursor_x = 50; gfx_cursor_y = 120;
                draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
                gfx_cursor_x += 32;
                swap_buffers();
            }
        } else {
            format_confirm_len = 0;
            format_confirm[0] = '\0';
        }
        return;
    } else if (c == '\b') {
        if (format_confirm_len > 0) {
            format_confirm[--format_confirm_len] = '\0';
        }
    } else if ((c >= 'A' && c <= 'Z') && format_confirm_len < 9) {
        format_confirm[format_confirm_len++] = c;
        format_confirm[format_confirm_len] = '\0';
    } else if ((c >= 'a' && c <= 'z') && format_confirm_len < 9) {
        format_confirm[format_confirm_len++] = c - ('a' - 'A');
        format_confirm[format_confirm_len] = '\0';
    }
    show_format_dialog();
}

void show_login_screen(void) {
    clear_screen_graphics(0x000A0C14);
    UINTN win_w = 480;
    UINTN win_h = 320;
    if (win_w > gop_width - 40) win_w = gop_width - 40;
    if (win_h > gop_height - 40) win_h = gop_height - 40;

    UINTN win_x = (gop_width - win_w) / 2;
    UINTN win_y = (gop_height - win_h) / 2;

    draw_window(win_x, win_y, win_w, win_h, "VeloOS - Secure Login");

    UINT32 col_u = (login_field_focus == 0) ? 0x0000FFCC : 0x00555566;
    draw_string("Benutzername:", win_x + 20, win_y + 55, 0x00FFFFFF, 0x00181A24);
    draw_rounded_rect_aa((int)(win_x + 20), (int)(win_y + 80), (int)(win_w - 40), 35, 6, 0x0024283B);
    draw_string(login_username, win_x + 30, win_y + 88, col_u, 0x0024283B);

    UINT32 col_p = (login_field_focus == 1) ? 0x0000FFCC : 0x00555566;
    draw_string("Passwort:", win_x + 20, win_y + 135, 0x00FFFFFF, 0x00181A24);
    draw_rounded_rect_aa((int)(win_x + 20), (int)(win_y + 160), (int)(win_w - 40), 35, 6, 0x0024283B);
    char masked[32]; int p = 0; while(login_password[p]) { masked[p++] = '*'; } masked[p] = '\0';
    draw_string(masked, win_x + 30, win_y + 168, col_p, 0x0024283B);

    UINT32 btn_bg = (login_field_focus == 2) ? 0x0000FF00 : 0x001B62D6;
    draw_rounded_rect_aa((int)(win_x + (win_w - 200) / 2), (int)(win_y + 225), 200, 40, 6, btn_bg);
    draw_string("Anmelden", win_x + (win_w - 200) / 2 + 40, win_y + 236, 0x00000000, btn_bg);

    draw_string("[TAB] Wechseln | [ENTER] OK | [ESC] Shell", win_x + 20, win_y + win_h - 35, 0x00777788, 0x00181A24);

    if (login_error) {
        draw_string("Falscher User oder Pass!", win_x + 25, win_y + 10, 0x00FF0000, 0x000E429C);
    }
    swap_buffers();
}

void show_account_creator_ui(void) {
    clear_screen_graphics(0x000A0C14);
    UINTN win_w = 480;
    UINTN win_h = 320;
    if (win_w > gop_width - 40) win_w = gop_width - 40;
    if (win_h > gop_height - 40) win_h = gop_height - 40;

    UINTN win_x = (gop_width - win_w) / 2;
    UINTN win_y = (gop_height - win_h) / 2;

    draw_window(win_x, win_y, win_w, win_h, "VeloOS - Account Setup Wizard");
    draw_string("Ersten Administrator anlegen:", win_x + 20, win_y + 50, 0x0000FFCC, 0x00181A24);
    
    draw_string("Username:", win_x + 20, win_y + 90, 0x00FFFFFF, 0x00181A24);
    draw_rounded_rect_aa((int)(win_x + 20), (int)(win_y + 115), (int)(win_w - 40), 30, 6, 0x0024283B);
    draw_string(ui_username, win_x + 30, win_y + 121, 0x00FFFFFF, 0x0024283B);

    draw_string("Password:", win_x + 20, win_y + 160, 0x00FFFFFF, 0x00181A24);
    draw_rounded_rect_aa((int)(win_x + 20), (int)(win_y + 185), (int)(win_w - 40), 30, 6, 0x0024283B);
    char masked[32]; int p = 0; while(ui_password[p]) { masked[p++] = '*'; } masked[p] = '\0';
    draw_string(masked, win_x + 30, win_y + 191, 0x00FFFFFF, 0x0024283B);

    UINT32 btn_bg = (ui_field_focus == 2) ? 0x0000FF00 : 0x001B62D6;
    draw_rounded_rect_aa((int)(win_x + (win_w - 200) / 2), (int)(win_y + 235), 200, 40, 6, btn_bg);
    draw_string("Speichern & Disk", win_x + (win_w - 200) / 2 + 20, win_y + 245, 0x00000000, btn_bg);
    draw_string("[ESC] Abbrechen zur Shell", win_x + 20, win_y + win_h - 35, 0x00777788, 0x00181A24);
    swap_buffers();
}

void perform_exit_boot_services(void) {
    if (is_bare_metal) return;

    mmap.MapSize = 0;
    EFI_STATUS status = g_st->BootServices->GetMemoryMap(
        &mmap.MapSize, NULL, &mmap.MapKey,
        &mmap.DescriptorSize, &mmap.DescriptorVersion);

    if (status != EFI_BUFFER_TOO_SMALL) return;

    mmap.MapSize += 2 * mmap.DescriptorSize;

    VOID *map_buffer = NULL;
    status = g_st->BootServices->AllocatePool(
        EfiLoaderData, mmap.MapSize, &map_buffer);
    if (status != EFI_SUCCESS || !map_buffer) return;

    mmap.Map = (EFI_MEMORY_DESCRIPTOR *)map_buffer;

    status = g_st->BootServices->GetMemoryMap(
        &mmap.MapSize, mmap.Map, &mmap.MapKey,
        &mmap.DescriptorSize, &mmap.DescriptorVersion);
    if (status != EFI_SUCCESS) return;

    status = g_st->BootServices->ExitBootServices(g_image_handle, mmap.MapKey);

    if (status == EFI_INVALID_PARAMETER) {
        mmap.MapSize = 0;
        status = g_st->BootServices->GetMemoryMap(
            &mmap.MapSize, NULL, &mmap.MapKey,
            &mmap.DescriptorSize, &mmap.DescriptorVersion);
        if (status != EFI_BUFFER_TOO_SMALL) return;

        mmap.MapSize += 2 * mmap.DescriptorSize;
        status = g_st->BootServices->GetMemoryMap(
            &mmap.MapSize, mmap.Map, &mmap.MapKey,
            &mmap.DescriptorSize, &mmap.DescriptorVersion);
        if (status != EFI_SUCCESS) return;

        status = g_st->BootServices->ExitBootServices(
            g_image_handle, mmap.MapKey);
    }

    if (status != EFI_SUCCESS) return;

    is_bare_metal = 1;
    system_mode = 0;
    logged_in = 0;
    selected_drive = -1;
    num_drives = 0;

    init_keyboard_bare_metal();

    clear_screen_graphics(0x00000000);
    draw_string("Scanne alle PCI Speicher-Controller...", 50, 30, 0x00FFFFFF, 0x00000000);
    swap_buffers();

    init_ahci(0);
    
    int port_count = ahci_get_port_count();
    
    for (int i = 0; i < port_count && num_drives < MAX_DRIVES; i++) {
        AHCI_PORT_INFO *info = ahci_get_port_info(i);
        if (!info || !info->active) continue;
        
        drives[num_drives].port = info->port_addr;
        drives[num_drives].port_number = info->port_number;
        drives[num_drives].sector_count = info->sector_count;
        
        UINT64 total_bytes = info->sector_count * 512;
        drives[num_drives].size_mb = (total_bytes > 0) ? (total_bytes / (1024 * 1024)) : 64;
        
        drives[num_drives].has_fat32 = fat32_init(info->port_addr);
        
        int pos = 0;
        drives[num_drives].label[pos++] = 'D';
        drives[num_drives].label[pos++] = 'r';
        drives[num_drives].label[pos++] = 'i';
        drives[num_drives].label[pos++] = 'v';
        drives[num_drives].label[pos++] = 'e';
        drives[num_drives].label[pos++] = ' ';
        drives[num_drives].label[pos++] = '0' + (char)num_drives;
        drives[num_drives].label[pos] = '\0';
        
        num_drives++;
    }

    if (num_drives == 0) {
        clear_screen_graphics(0x00000000);
        draw_string("VeloOS Kernel - Bare Metal Mode Active", 50, 30, 0x00FFFFFF, 0x00000000);
        draw_string("WARNUNG: Keine Laufwerke gefunden.", 50, 60, 0x00FFAA00, 0x00000000);
        gfx_cursor_x = 50; gfx_cursor_y = 120;
        draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
        gfx_cursor_x += 32;
        swap_buffers();
    } else if (num_drives == 1) {
        selected_drive = 0;
        if (drives[0].has_fat32) {
            clear_screen_graphics(0x00000000);
            draw_string("VeloOS Kernel - Bare Metal Mode Active", 50, 30, 0x00FFFFFF, 0x00000000);
            draw_string("Laufwerk bereit.", 50, 60, 0x0000FFCC, 0x00000000);
            gfx_cursor_x = 50; gfx_cursor_y = 120;
            draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
            gfx_cursor_x += 32;
            swap_buffers();
        } else {
            format_mode = 1;
            format_confirm_len = 0;
            format_confirm[0] = '\0';
            show_format_dialog();
        }
    } else {
        selected_drive = 0;
        drive_select_mode = 1;
        show_drive_select_dialog();
    }
}

void execute_command(void) {
    if (command_length == 0) return;
    command_buffer[command_length] = '\0';

    gfx_cursor_x = 50;
    gfx_cursor_y += 36;

    if (strcmp(command_buffer, "clear") == 0) {
        clear_screen_graphics(0x00000000);
        draw_string("VeloOS Kernel - Bare Metal Mode Active", 50, 30, 0x00FFFFFF, 0x00000000);
        gfx_cursor_y = 120; gfx_cursor_x = 50;
        draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
        gfx_cursor_x += 32;
        command_length = 0;
        swap_buffers();
        return;
    }
    else if (strcmp(command_buffer, "login") == 0) {
        if (!is_bare_metal) {
            draw_string("Erst mit 'exit' in den Bare-Metal-Modus wechseln!", gfx_cursor_x, gfx_cursor_y, 0x00FF0000, 0x00000000);
        } else if (selected_drive < 0 || !check_account_exists()) {
            draw_string("Fehler: Kein Account auf Disk vorhanden.", gfx_cursor_x, gfx_cursor_y, 0x00FF0000, 0x00000000);
        } else {
            system_mode = 1;
            login_field_focus = 0;
            login_error = 0;
            __builtin_memset(login_username, 0, 32);
            __builtin_memset(login_password, 0, 32);
            login_len_u = 0;
            login_len_p = 0;
            show_login_screen();
            command_length = 0;
            return;
        }
    }
    else if (strcmp(command_buffer, "desktop") == 0) {
        if (!is_bare_metal) {
            draw_string("Erst mit 'exit' in den Bare-Metal-Modus wechseln!", gfx_cursor_x, gfx_cursor_y, 0x00FF0000, 0x00000000);
        } else if (!check_account_exists()) {
            draw_string("Fehler: Kein Account vorhanden. Nutze 'create account'.", gfx_cursor_x, gfx_cursor_y, 0x00FF0000, 0x00000000);
        } else if (!logged_in) {
            draw_string("Fehler: Nicht eingeloggt. Nutze zuerst 'login'.", gfx_cursor_x, gfx_cursor_y, 0x00FF0000, 0x00000000);
        } else {
            desktop_start();
            command_length = 0;
            return;
        }
    }
    else if (strcmp(command_buffer, "create account") == 0) {
        if (!is_bare_metal) {
            draw_string("Erst mit 'exit' in den Bare-Metal-Modus wechseln!", gfx_cursor_x, gfx_cursor_y, 0x00FF0000, 0x00000000);
        } else if (check_account_exists()) {
            draw_string("Fehler: Account existiert bereits auf Disk! Bitte 'login' nutzen.", gfx_cursor_x, gfx_cursor_y, 0x00FF0000, 0x00000000);
        } else {
            system_mode = 2;
            ui_field_focus = 0;
            __builtin_memset(ui_username, 0, 32);
            __builtin_memset(ui_password, 0, 32);
            ui_input_len_u = 0; ui_input_len_p = 0;
            show_account_creator_ui();
            command_length = 0;
            return;
        }
    }
    else if (strcmp(command_buffer, "shutdown") == 0) {
        draw_string("Fahre System herunter...", gfx_cursor_x, gfx_cursor_y, 0x00FFAA00, 0x00000000);
        swap_buffers();
        system_shutdown();
    }
    else if (strcmp(command_buffer, "reboot") == 0) {
        draw_string("Starte System neu...", gfx_cursor_x, gfx_cursor_y, 0x00FFAA00, 0x00000000);
        swap_buffers();
        system_reboot();
    }
    else if (strcmp(command_buffer, "exit") == 0) {
        if (is_bare_metal) {
            draw_string("Bereits im Bare-Metal-Modus!", gfx_cursor_x, gfx_cursor_y, 0x00FF0000, 0x00000000);
        } else {
            draw_string("Wechsle in den Bare-Metal-Modus...", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
            swap_buffers();
            perform_exit_boot_services();
            command_length = 0;
            return;
        }
    }
    else if (strcmp(command_buffer, "help") == 0) {
        draw_string("Befehle: exit, create account, login, desktop,", gfx_cursor_x, gfx_cursor_y, 0x00FFFF00, 0x00000000);
        gfx_cursor_y += 24;
        draw_string("         shutdown, reboot, clear, help", gfx_cursor_x, gfx_cursor_y, 0x00FFFF00, 0x00000000);
    } else {
        draw_string("Unbekannter Befehl. Tippe 'help'.", gfx_cursor_x, gfx_cursor_y, 0x00FF0000, 0x00000000);
    }

    gfx_cursor_x = 50;
    gfx_cursor_y += 36;
    draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
    gfx_cursor_x += 32;
    command_length = 0;
    swap_buffers();
}

void handle_login_key(char c) {
    if (c == 0x1B) {
        system_mode = 0;
        clear_screen_graphics(0x00000000);
        draw_string("VeloOS Kernel - Bare Metal Mode Active", 50, 30, 0x00FFFFFF, 0x00000000);
        draw_string("Zurueck in der Shell.", 50, 60, 0x0000FFCC, 0x00000000);
        gfx_cursor_x = 50; gfx_cursor_y = 120;
        draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
        gfx_cursor_x += 32;
        swap_buffers();
        return;
    }
    if (c == '\t') {
        login_field_focus = (login_field_focus + 1) % 3;
    } else if (c == '\n' || c == '\r') {
        if (login_field_focus < 2) {
            login_field_focus++;
        } else {
            if (verify_login(login_username, login_password)) {
                __builtin_memset(login_password, 0, 32);
                logged_in = 1;
                system_mode = 0;
                clear_screen_graphics(0x00000000);
                draw_string("VeloOS Kernel - Bare Metal Mode Active", 50, 30, 0x00FFFFFF, 0x00000000);
                draw_string("Login erfolgreich. Tippe 'desktop' zum Starten des Desktops.", 50, 60, 0x0000FFCC, 0x00000000);
                gfx_cursor_x = 50;
                gfx_cursor_y = 120;
                draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
                gfx_cursor_x += 32;
                swap_buffers();
                return;
            } else {
                login_error = 1;
            }
        }
    } else if (c == '\b') {
        if (login_field_focus == 0 && login_len_u > 0) login_username[--login_len_u] = '\0';
        else if (login_field_focus == 1 && login_len_p > 0) login_password[--login_len_p] = '\0';
    } else if (c >= 32 && c <= 126) {
        if (login_field_focus == 0 && login_len_u < 31) { login_username[login_len_u++] = c; login_username[login_len_u] = '\0'; }
        else if (login_field_focus == 1 && login_len_p < 31) { login_password[login_len_p++] = c; login_password[login_len_p] = '\0'; }
    }
    show_login_screen();
}

void handle_account_ui_key(char c) {
    if (c == 0x1B) {
        system_mode = 0;
        clear_screen_graphics(0x00000000);
        draw_string("VeloOS Kernel - Bare Metal Mode Active", 50, 30, 0x00FFFFFF, 0x00000000);
        draw_string("Account Setup abgebrochen.", 50, 60, 0x00FF0000, 0x00000000);
        gfx_cursor_x = 50; gfx_cursor_y = 120;
        draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
        gfx_cursor_x += 32;
        swap_buffers();
        return;
    }
    if (c == '\t') {
        ui_field_focus = (ui_field_focus + 1) % 3;
    } else if (c == '\n' || c == '\r') {
        if (ui_field_focus < 2) {
            ui_field_focus++;
        } else {
            int saved = save_account_to_disk(ui_username, ui_password);
            __builtin_memset(ui_password, 0, 32);
            __builtin_memset(ui_username, 0, 32);

            system_mode = 0;
            clear_screen_graphics(0x00000000);
            draw_string("VeloOS Kernel - Bare Metal Mode Active", 50, 30, 0x00FFFFFF, 0x00000000);
            if (saved) {
                draw_string("Account erfolgreich auf Disk gespeichert.", 50, 60, 0x0000FFCC, 0x00000000);
            } else {
                draw_string("FEHLER: Account konnte nicht auf Disk gespeichert werden.", 50, 60, 0x00FF0000, 0x00000000);
            }
            gfx_cursor_x = 50; gfx_cursor_y = 120;
            draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
            gfx_cursor_x += 32;
            swap_buffers();
            return;
        }
    } else if (c == '\b') {
        if (ui_field_focus == 0 && ui_input_len_u > 0) ui_username[--ui_input_len_u] = '\0';
        else if (ui_field_focus == 1 && ui_input_len_p > 0) ui_password[--ui_input_len_p] = '\0';
    } else if (c >= 32 && c <= 126) {
        if (ui_field_focus == 0 && ui_input_len_u < 31) { ui_username[ui_input_len_u++] = c; ui_username[ui_input_len_u] = '\0'; }
        else if (ui_field_focus == 1 && ui_input_len_p < 31) { ui_password[ui_input_len_p++] = c; ui_password[ui_input_len_p] = '\0'; }
    }
    show_account_creator_ui();
}

void handle_shell_key(char c) {
    if (c == '\n' || c == '\r') execute_command();
    else if (c == '\b') {
        if (command_length > 0 && gfx_cursor_x > 82) {
            command_length--;
            gfx_cursor_x -= 16;
            draw_char(' ', gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
            swap_buffers();
        }
    } else if (c >= 32 && c <= 126) {
        if (command_length < 61) {
            command_buffer[command_length++] = c;
            draw_char(c, gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
            gfx_cursor_x += 16;
            swap_buffers();
        }
    }
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    g_st = SystemTable;
    g_image_handle = ImageHandle;
    InitializeLib(ImageHandle, SystemTable);

    init_gop();
    clear_screen_graphics(0x00000000);

    draw_string("VeloOS Bootloader aktiv. Tippe 'exit' fuer Bare-Metal.", 50, 50, 0x0000FFCC, 0x00000000);
    gfx_cursor_x = 50; gfx_cursor_y = 100;
    draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
    gfx_cursor_x += 32;
    swap_buffers();

    init_keyboard();

    /* 60 FPS Event-Loop mit sofortiger Auflösungserkennung */
    while (1) {
        check_screen_resolution_change();

        char ascii = poll_keyboard_ascii();
        if (ascii != 0) {
            if (drive_select_mode) {
                handle_drive_select_key(ascii);
            } else if (format_mode) {
                handle_format_key(ascii);
            } else if (system_mode == 1) {
                handle_login_key(ascii);
            } else if (system_mode == 2) {
                handle_account_ui_key(ascii);
            } else if (system_mode == 3) {
                desktop_handle_key(ascii);
            } else {
                handle_shell_key(ascii);
            }
        }

        if (system_mode == 3) {
            desktop_tick_frame();
        }
    }
    return EFI_SUCCESS;
}
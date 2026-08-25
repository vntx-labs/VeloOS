#include "syscall.h"
#include "wm.h"
#include "ahci.h"
#include "fat32.h"
#include "mouse.h"
#include "keyboard.h"
#include "desktop.h"

extern UINTN gop_width;
extern UINTN gop_height;
extern UINT64 g_total_ram_mb;
extern char g_cpu_brand[49];
extern char g_user_name_active[32];
extern char g_pc_name_active[32];

void swap_buffers(void);

static UINT8 g_app_buffer[131072] __attribute__((aligned(4096)));
static UINT8 g_exec_area[262144] __attribute__((aligned(4096)));
static UINT8 g_user_heap[1048576] __attribute__((aligned(4096)));
static UINTN g_heap_offset = 0;
static int g_app_win_id = -1;

typedef struct {
    UINT8  e_ident[16];
    UINT16 e_type;
    UINT16 e_machine;
    UINT32 e_version;
    UINT64 e_entry;
    UINT64 e_phoff;
    UINT64 e_shoff;
    UINT32 e_flags;
    UINT16 e_ehsize;
    UINT16 e_phentsize;
    UINT16 e_phnum;
    UINT16 e_shentsize;
    UINT16 e_shnum;
    UINT16 e_shstrndx;
} __attribute__((packed)) Elf64_Ehdr;

typedef struct {
    UINT32 p_type;
    UINT32 p_flags;
    UINT64 p_offset;
    UINT64 p_vaddr;
    UINT64 p_paddr;
    UINT64 p_filesz;
    UINT64 p_memsz;
    UINT64 p_align;
} __attribute__((packed)) Elf64_Phdr;

/* Syscall Dispatcher */
UINT64 syscall_handler_c(UINT64 num, UINT64 a1, UINT64 a2, UINT64 a3, UINT64 a4) {
    switch (num) {
        case SYS_EXIT: {
            if (g_app_win_id >= 0) {
                wm_close_window(g_app_win_id);
                g_app_win_id = -1;
                desktop_tick_frame();
            }
            return 0;
        }

        case SYS_CREATE_WINDOW: {
            const char *title = (const char *)a1;
            int w = (int)a2;
            int h = (int)a3;
            g_app_win_id = wm_create_window_auto(title, w, h, NULL);
            desktop_tick_frame();
            return (UINT64)g_app_win_id;
        }

        case SYS_GET_WIN_SIZE: {
            int win_id = (int)a1;
            Window *win = wm_get_window(win_id);
            if (!win || win->is_closed) return 0;
            int cw = win->is_maximized ? win->width : win->width - 12;
            int ch = win->is_maximized ? win->height - 30 : win->height - 36;
            return ((UINT64)cw << 32) | (UINT32)ch;
        }

        case SYS_CLEAR_WINDOW: {
            int win_id = (int)a1;
            Window *win = wm_get_window(win_id);
            if (!win || win->is_closed) return (UINT64)-1;
            wm_surface_clear(win_id, win->bg_color);
            return 0;
        }

        case SYS_DRAW_RECT: {
            int win_id = (int)a1;
            int rx = (int)(a2 >> 32);
            int ry = (int)(a2 & 0xFFFFFFFF);
            int rw = (int)(a3 >> 32);
            int rh = (int)(a3 & 0xFFFFFFFF);
            wm_surface_draw_rect(win_id, rx, ry, rw, rh, 0x0093C5FD);
            return 0;
        }

        case SYS_DRAW_RECT_COL: {
            int win_id = (int)a1;
            int rx = (int)(a2 >> 32);
            int ry = (int)(a2 & 0xFFFFFFFF);
            int rw = (int)(a3 >> 32);
            int rh = (int)(a3 & 0xFFFFFFFF);
            UINT32 col = (UINT32)a4;
            wm_surface_draw_rect(win_id, rx, ry, rw, rh, col);
            return 0;
        }

        case SYS_DRAW_GRADIENT: {
            int win_id = (int)a1;
            int rx = (int)(a2 >> 32);
            int ry = (int)(a2 & 0xFFFFFFFF);
            int rw = (int)(a3 >> 32);
            int rh = (int)(a3 & 0xFFFFFFFF);
            UINT32 top_col = (UINT32)(a4 >> 32);
            UINT32 bot_col = (UINT32)(a4 & 0xFFFFFFFF);
            wm_surface_draw_gradient(win_id, rx, ry, rw, rh, top_col, bot_col);
            return 0;
        }

        case SYS_DRAW_TEXT: {
            int win_id = (int)a1;
            const char *text = (const char *)a2;
            int tx = (int)a3;
            int ty = (int)a4;
            wm_surface_draw_text(win_id, text, tx, ty, 0x000F172A);
            return 0;
        }

        case SYS_DRAW_TEXT_COL: {
            int win_id = (int)a1;
            const char *text = (const char *)a2;
            int tx = (int)a3;
            int ty = (int)(a4 >> 32);
            UINT32 col = (UINT32)(a4 & 0xFFFFFFFF);
            wm_surface_draw_text(win_id, text, tx, ty, col);
            return 0;
        }

        case SYS_GET_EVENT: {
            int win_id = (int)a1;
            UserEvent *ev = (UserEvent *)a2;
            if (!ev) return (UINT64)-1;

            Window *win = wm_get_window(win_id);
            if (!win || win->is_closed) {
                return (UINT64)-1;
            }

            if (win->is_dirty_content) {
                win->is_dirty_content = 0;
                ev->type = 3;
                ev->x = win->is_maximized ? win->width : win->width - 12;
                ev->y = win->is_maximized ? win->height - 30 : win->height - 36;
                return 1;
            }

            desktop_tick_frame();
            MouseState *m = mouse_get_state();

            if (ST && ST->ConIn) {
                EFI_INPUT_KEY k;
                if (uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &k) == EFI_SUCCESS) {
                    if (k.ScanCode == 0x17) {
                        wm_close_window(win_id);
                        desktop_tick_frame();
                        return (UINT64)-1;
                    }
                    if (k.UnicodeChar >= 32 && k.UnicodeChar <= 126) {
                        ev->type = 2;
                        ev->key = keyboard_translate_char((char)k.UnicodeChar);
                        return 1;
                    } else if (k.UnicodeChar == '\b' || k.UnicodeChar == '\r' || k.UnicodeChar == '\n') {
                        ev->type = 2;
                        ev->key = (k.UnicodeChar == '\b') ? '\b' : '\n';
                        return 1;
                    }
                }
            }

            if (m->left_clicked) {
                int btn_top = win->is_maximized ? 4 : 5;
                if (m->x >= win->x + win->width - 30 && m->x <= win->x + win->width - 4 &&
                    m->y >= win->y + btn_top && m->y <= win->y + btn_top + 20) {
                    wm_close_window(win_id);
                    desktop_tick_frame();
                    return (UINT64)-1;
                }

                if (m->x >= win->x + win->width - 58 && m->x <= win->x + win->width - 32 &&
                    m->y >= win->y + btn_top && m->y <= win->y + btn_top + 20) {
                    wm_maximize_window(win_id);
                    desktop_tick_frame();
                    ev->type = 3;
                    ev->x = win->is_maximized ? win->width : win->width - 12;
                    ev->y = win->is_maximized ? win->height - 30 : win->height - 36;
                    return 1;
                }

                if (m->x >= win->x + win->width - 86 && m->x <= win->x + win->width - 60 &&
                    m->y >= win->y + btn_top && m->y <= win->y + btn_top + 20) {
                    wm_minimize_window(win_id);
                    desktop_tick_frame();
                    return 0;
                }

                int off_x = win->is_maximized ? 0 : 6;
                int off_y = 30;
                int cw = win->is_maximized ? win->width : win->width - 12;
                int ch = win->is_maximized ? win->height - 30 : win->height - 36;

                if (m->x >= win->x + off_x && m->x <= win->x + off_x + cw &&
                    m->y >= win->y + off_y && m->y <= win->y + off_y + ch) {
                    ev->type = 1;
                    ev->x = m->x - (win->x + off_x);
                    ev->y = m->y - (win->y + off_y);
                    return 1;
                }
            }
            return 0;
        }

        case SYS_MARK_DIRTY: {
            wm_mark_all_dirty();
            desktop_tick_frame();
            return 0;
        }

        case SYS_HEAP_ALLOC: {
            UINTN bytes = (UINTN)a1;
            bytes = (bytes + 15) & ~15;
            if (g_heap_offset + bytes > sizeof(g_user_heap)) return 0;
            void *ptr = &g_user_heap[g_heap_offset];
            g_heap_offset += bytes;
            return (UINT64)(UINTN)ptr;
        }

        case SYS_READ_FILE: {
            const char *filename = (const char *)a1;
            void *buf = (void *)a2;
            UINT32 max_len = (UINT32)a3;
            int port_count = ahci_get_port_count();
            for (int p = 0; p < port_count; p++) {
                AHCI_PORT_INFO *info = ahci_get_port_info(p);
                if (!info || !info->active) continue;
                return (UINT64)fat32_read_file(info->port_addr, filename, buf, max_len);
            }
            return (UINT64)-1;
        }

        case SYS_WRITE_FILE: {
            const char *filename = (const char *)a1;
            void *buf = (void *)a2;
            UINT32 size = (UINT32)a3;
            int port_count = ahci_get_port_count();
            for (int p = 0; p < port_count; p++) {
                AHCI_PORT_INFO *info = ahci_get_port_info(p);
                if (!info || !info->active) continue;
                return (UINT64)fat32_write_file(info->port_addr, filename, buf, size);
            }
            return 0;
        }

        case SYS_LIST_FILES: {
            const char *path = (const char *)a1;
            VeloDirEntry *out_entries = (VeloDirEntry *)a2;
            int max_count = (int)a3;
            int port_count = ahci_get_port_count();
            for (int p = 0; p < port_count; p++) {
                AHCI_PORT_INFO *info = ahci_get_port_info(p);
                if (!info || !info->active) continue;
                return (UINT64)fat32_list_dir(info->port_addr, path, out_entries, max_count);
            }
            return 0;
        }

        case SYS_EXEC_APP: {
            const char *app_name = (const char *)a1;
            return (UINT64)load_and_run_app(app_name);
        }

        case SYS_GET_SYSINFO: {
            VeloSysInfo *info = (VeloSysInfo *)a1;
            if (!info) return (UINT64)-1;
            __builtin_memcpy(info->cpu_brand, g_cpu_brand, sizeof(info->cpu_brand));
            __builtin_memcpy(info->user_name, g_user_name_active, sizeof(info->user_name));
            __builtin_memcpy(info->pc_name, g_pc_name_active, sizeof(info->pc_name));
            info->total_ram_mb = g_total_ram_mb;
            return 0;
        }

        case SYS_GET_DRIVE_INFO: {
            int drive_idx = (int)a1;
            VeloDriveInfo *dinfo = (VeloDriveInfo *)a2;
            if (!dinfo) return (UINT64)-1;
            int port_count = ahci_get_port_count();
            if (drive_idx < 0 || drive_idx >= port_count) return (UINT64)-1;

            AHCI_PORT_INFO *pinfo = ahci_get_port_info(drive_idx);
            if (!pinfo || !pinfo->active) return (UINT64)-1;

            __builtin_memcpy(dinfo->model, pinfo->model, sizeof(dinfo->model));
            dinfo->label[0] = (char)('C' + drive_idx);
            dinfo->label[1] = ':';
            dinfo->label[2] = '\0';
            dinfo->total_bytes = pinfo->sector_count * 512ULL;
            dinfo->free_bytes = fat32_get_free_bytes(pinfo->port_addr);
            dinfo->is_removable = 0;
            return 0;
        }
    }
    return (UINT64)-1;
}

void init_ring3_and_syscalls(void) {
    g_heap_offset = 0;
    *(volatile UINT64*)(SYSCALL_VECTOR_ADDR) = (UINT64)syscall_handler_c;
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

    if (g_app_buffer[0] == 0x7F && g_app_buffer[1] == 'E' && 
        g_app_buffer[2] == 'L' && g_app_buffer[3] == 'F') {
        
        Elf64_Ehdr *ehdr = (Elf64_Ehdr *)g_app_buffer;
        __builtin_memset(g_exec_area, 0, sizeof(g_exec_area));

        Elf64_Phdr *phdrs = (Elf64_Phdr *)(g_app_buffer + ehdr->e_phoff);
        for (int i = 0; i < ehdr->e_phnum; i++) {
            if (phdrs[i].p_type == 1) {
                if (phdrs[i].p_vaddr + phdrs[i].p_filesz <= sizeof(g_exec_area)) {
                    __builtin_memcpy(g_exec_area + phdrs[i].p_vaddr, 
                                     g_app_buffer + phdrs[i].p_offset, 
                                     phdrs[i].p_filesz);
                }
            }
        }

        AppMain app_entry = (AppMain)(void*)(g_exec_area + ehdr->e_entry);
        app_entry();
        return 1;
    }

    AppMain app_entry = (AppMain)(void*)g_app_buffer;
    app_entry();
    return 1;
}
#include "syscall.h"
#include "wm.h"
#include "ahci.h"
#include "fat32.h"
#include "mouse.h"
#include "keyboard.h"
#include "desktop.h"
#include "net.h"
#include "sched.h"
#include "pmm_vmm.h"

extern UINTN gop_width;
extern UINTN gop_height;
extern UINT64 g_total_ram_mb;
extern char g_cpu_brand[49];
extern char g_user_name_active[32];
extern char g_pc_name_active[32];
extern UINT8 g_task_user_stacks[MAX_TASKS][TASK_STACK_SIZE];
extern Task g_tasks[MAX_TASKS];

static inline void outb_io(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw_io(unsigned short port, unsigned short val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

void swap_buffers(void);

#define APP_MAX_SIZE 2097152 // 2 MB pro App

static UINT8 g_app_buffer[APP_MAX_SIZE] __attribute__((aligned(4096)));
static UINT8 g_app_exec_memory[MAX_TASKS][APP_MAX_SIZE] __attribute__((aligned(4096)));
static UINT8 g_user_heap[16777216] __attribute__((aligned(4096))); // 16 MB Userland Heap
static UINTN g_heap_offset = 0;
static int g_app_win_ids[MAX_TASKS] = {0};

static inline void kstr_concat(char *dst, const char *prefix, const char *name, UINTN max_len) {
    UINTN pos = 0;
    while (*prefix && pos < max_len - 1) dst[pos++] = *prefix++;
    while (*name && pos < max_len - 1) dst[pos++] = *name++;
    dst[pos] = '\0';
}

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

typedef struct {
    UINT32 sh_name;
    UINT32 sh_type;
    UINT64 sh_flags;
    UINT64 sh_addr;
    UINT64 sh_offset;
    UINT64 sh_size;
    UINT32 sh_link;
    UINT32 sh_info;
    UINT64 sh_addralign;
    UINT64 sh_entsize;
} __attribute__((packed)) Elf64_Shdr;

typedef struct {
    UINT64 r_offset;
    UINT64 r_info;
    INT64  r_addend;
} __attribute__((packed)) Elf64_Rela;

typedef struct {
    UINT32 st_name;
    UINT8  st_info;
    UINT8  st_other;
    UINT16 st_shndx;
    UINT64 st_value;
    UINT64 st_size;
} __attribute__((packed)) Elf64_Sym;

static void* get_port_from_path(const char *path) {
    int port_count = ahci_get_port_count();
    if (port_count == 0) return NULL;

    if (path && ((path[0] >= 'C' && path[0] <= 'Z') || (path[0] >= 'c' && path[0] <= 'z')) && path[1] == ':') {
        int idx = (path[0] >= 'a') ? (path[0] - 'c') : (path[0] - 'C');
        if (idx >= 0 && idx < port_count) {
            AHCI_PORT_INFO *info = ahci_get_port_info(idx);
            if (info && info->active) return info->port_addr;
        }
    }

    AHCI_PORT_INFO *info = ahci_get_port_info(0);
    if (info && info->active) return info->port_addr;
    return NULL;
}

void enable_user_paging(void) {
    UINT64 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 16); // WP Bit aus
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    UINT64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    UINT64 *pml4 = (UINT64*)(UINTN)(cr3 & 0x000FFFFFFFFFF000ULL);
    if (!pml4) return;

    pml4[0] |= (1ULL << 2) | (1ULL << 1);

    UINT64 *pdpt = (UINT64*)(UINTN)(pml4[0] & 0x000FFFFFFFFFF000ULL);
    if (pdpt) {
        for (int i = 0; i < 4; i++) {
            if (pdpt[i] & 1) {
                pdpt[i] |= (1ULL << 2) | (1ULL << 1);

                if (!(pdpt[i] & 0x80)) {
                    UINT64 *pd = (UINT64*)(UINTN)(pdpt[i] & 0x000FFFFFFFFFF000ULL);
                    if (pd) {
                        for (int j = 0; j < 512; j++) {
                            if (pd[j] & 1) {
                                pd[j] |= (1ULL << 2) | (1ULL << 1);

                                if (!(pd[j] & 0x80)) {
                                    UINT64 *pt = (UINT64*)(UINTN)(pd[j] & 0x000FFFFFFFFFF000ULL);
                                    if (pt) {
                                        for (int k = 0; k < 512; k++) {
                                            if (pt[k] & 1) {
                                                pt[k] |= (1ULL << 2) | (1ULL << 1);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3));
}

UINT64 syscall_handler_c(UINT64 num, UINT64 a1, UINT64 a2, UINT64 a3, UINT64 a4) {
    int cur_task = task_get_current_id();

    switch (num) {
        case SYS_EXIT: {
            if (cur_task >= 0 && cur_task < MAX_TASKS && g_app_win_ids[cur_task] >= 0) {
                wm_close_window(g_app_win_ids[cur_task]);
                g_app_win_ids[cur_task] = -1;
            }
            task_exit();
            return 0;
        }

        case SYS_CREATE_WINDOW: {
            const char *title = (const char *)a1;
            int w = (int)a2;
            int h = (int)a3;
            int win_id = wm_create_window_auto(title, w, h, NULL);
            if (cur_task >= 0 && cur_task < MAX_TASKS) g_app_win_ids[cur_task] = win_id;
            wm_mark_all_dirty();
            return (UINT64)win_id;
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

        case SYS_DRAW_TEXT: {
            int win_id = (int)a1;
            const char *text = (const char *)a2;
            int tx = (int)a3;
            int ty = (int)a4;
            wm_surface_draw_text(win_id, text, tx, ty, 0x000F172A);
            return 0;
        }

        case SYS_GET_EVENT: {
            int win_id = (int)a1;
            UserEvent *ev = (UserEvent *)a2;
            if (!ev) return (UINT64)-1;

            Window *win = wm_get_window(win_id);
            if (!win || win->is_closed) return (UINT64)-1;

            if (win->is_dirty_content) {
                win->is_dirty_content = 0;
                ev->type = VELO_EV_RESIZE;
                ev->x = win->is_maximized ? win->width : win->width - 12;
                ev->y = win->is_maximized ? win->height - 30 : win->height - 36;
                return 1;
            }

            WinEvent wev;
            if (wm_window_pop_event(win_id, &wev)) {
                ev->type = wev.type;
                ev->x = wev.x;
                ev->y = wev.y;
                ev->key = wev.key;
                return 1;
            }
            return 0;
        }

        case SYS_MARK_DIRTY: {
            wm_mark_all_dirty();
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
            void *port = get_port_from_path(filename);
            if (!port) return (UINT64)-1;
            return (UINT64)fat32_read_file(port, filename, buf, max_len);
        }

        case SYS_WRITE_FILE: {
            const char *filename = (const char *)a1;
            void *buf = (void *)a2;
            UINT32 size = (UINT32)a3;
            void *port = get_port_from_path(filename);
            if (!port) return 0;
            return (UINT64)fat32_write_file(port, filename, buf, size);
        }

        case SYS_LIST_FILES: {
            const char *path = (const char *)a1;
            VeloDirEntry *out_entries = (VeloDirEntry *)a2;
            int max_count = (int)a3;
            void *port = get_port_from_path(path);
            if (!port) return 0;
            return (UINT64)fat32_list_dir(port, path, out_entries, max_count);
        }

        case SYS_EXEC_APP: {
            const char *app_name = (const char *)a1;
            return (UINT64)task_spawn_app(app_name);
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

        case SYS_DRAW_TEXT_COL: {
            int win_id = (int)a1;
            const char *text = (const char *)a2;
            int tx = (int)a3;
            int ty = (int)(a4 >> 32);
            UINT32 col = (UINT32)(a4 & 0xFFFFFFFF);
            wm_surface_draw_text(win_id, text, tx, ty, col);
            return 0;
        }

        case SYS_GET_WIN_SIZE: {
            int win_id = (int)a1;
            Window *win = wm_get_window(win_id);
            if (!win || win->is_closed) return 0;
            int cw = win->is_maximized ? win->width : win->width - 12;
            int ch = win->is_maximized ? win->height - 30 : win->height - 36;
            return ((UINT64)cw << 32) | (UINT32)ch;
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
            dinfo->total_bytes = (UINT64)pinfo->sector_count * 512ULL;
            dinfo->free_bytes = fat32_get_free_bytes(pinfo->port_addr);
            dinfo->is_removable = 0;
            return 0;
        }

        case SYS_SOCKET_OPEN: {
            const char *host = (const char *)a1;
            UINT16 port = (UINT16)a2;
            int use_tls = (int)a3;
            return (UINT64)net_socket_open(host, port, use_tls);
        }

        case SYS_SOCKET_SEND: {
            int sock_id = (int)a1;
            const void *data = (const void *)a2;
            int len = (int)a3;
            return (UINT64)net_socket_send(sock_id, data, len);
        }

        case SYS_SOCKET_RECV: {
            int sock_id = (int)a1;
            void *buf = (void *)a2;
            int max_len = (int)a3;
            return (UINT64)net_socket_recv(sock_id, buf, max_len);
        }

        case SYS_SOCKET_CLOSE: {
            int sock_id = (int)a1;
            net_socket_close(sock_id);
            return 0;
        }

        case SYS_HTTP_GET: {
            const char *url = (const char *)a1;
            char *out_buf = (char *)a2;
            int max_len = (int)a3;
            if (!url || !out_buf || max_len <= 0) return 0;
            return (UINT64)net_http_get(url, out_buf, max_len);
        }

        // ==============================================================
        // HIER WURDEN DIE BEIDEN FEHLENDEN ASYNC-HTTP HANDLER EINGEFÜGT!
        // ==============================================================
        case SYS_HTTP_ASYNC_START: {
            const char *url = (const char *)a1;
            return (UINT64)net_http_async_start(url);
        }

        case SYS_HTTP_ASYNC_POLL: {
            char *out_buf = (char *)a1;
            int max_len = (int)a2;
            int *out_status = (int *)a3;
            int *out_bytes = (int *)a4;
            return (UINT64)net_http_async_poll(out_buf, max_len, out_status, out_bytes);
        }

        case SYS_DNS_RESOLVE: {
            const char *hostname = (const char *)a1;
            return (UINT64)net_dns_resolve(hostname);
        }

        case SYS_TASK_SLEEP: {
            UINT32 ticks = (UINT32)a1;
            task_sleep(ticks);
            return 0;
        }

        case SYS_TASK_YIELD: {
            task_yield();
            return 0;
        }

        case SYS_DELETE_FILE: {
            const char *filename = (const char *)a1;
            void *port = get_port_from_path(filename);
            if (!port) return 0;
            return (UINT64)fat32_delete_file(port, filename);
        }

        case SYS_COPY_FILE: {
            const char *src = (const char *)a1;
            const char *dst = (const char *)a2;
            void *src_port = get_port_from_path(src);
            void *dst_port = get_port_from_path(dst);
            if (!src_port || !dst_port) return 0;
            
            int bytes = fat32_read_file(src_port, src, g_app_buffer, sizeof(g_app_buffer));
            if (bytes < 0) return 0;
            fat32_delete_file(dst_port, dst);
            return (UINT64)fat32_write_file(dst_port, dst, g_app_buffer, (UINT32)bytes);
        }

        case SYS_MOVE_FILE: {
            const char *src = (const char *)a1;
            const char *dst = (const char *)a2;
            void *src_port = get_port_from_path(src);
            void *dst_port = get_port_from_path(dst);
            if (!src_port || !dst_port) return 0;

            int bytes = fat32_read_file(src_port, src, g_app_buffer, sizeof(g_app_buffer));
            if (bytes < 0) return 0;
            fat32_delete_file(dst_port, dst);
            if (!fat32_write_file(dst_port, dst, g_app_buffer, (UINT32)bytes)) return 0;
            return (UINT64)fat32_delete_file(src_port, src);
        }

        case SYS_MKDIR: {
            const char *path = (const char *)a1;
            void *port = get_port_from_path(path);
            if (!port) return 0;
            return (UINT64)fat32_mkdir(port, path);
        }

        case SYS_RENAME_FILE: {
            const char *old_path = (const char *)a1;
            const char *new_name = (const char *)a2;
            void *port = get_port_from_path(old_path);
            if (!port) return 0;
            return (UINT64)fat32_rename_file(port, old_path, new_name);
        }

        case SYS_DRAW_ICON: {
            int win_id = (int)a1;
            int icon_type = (int)a2;
            int ix = (int)(a3 >> 32);
            int iy = (int)(a3 & 0xFFFFFFFF);
            int isz = (int)a4;
            wm_surface_draw_icon(win_id, icon_type, ix, iy, isz);
            return 0;
        }

        case SYS_DRAW_BUTTON: {
            int win_id = (int)a1;
            int rx = (int)(a2 >> 32);
            int ry = (int)(a2 & 0xFFFFFFFF);
            int rw = (int)(a3 >> 32);
            int rh = (int)(a3 & 0xFFFFFFFF);
            const char *label = (const char *)a4;
            wm_surface_draw_button(win_id, rx, ry, rw, rh, label);
            return 0;
        }

        case SYS_DRAW_STORAGE_BAR: {
            int win_id = (int)a1;
            int rx = (int)(a2 >> 32);
            int ry = (int)(a2 & 0xFFFFFFFF);
            int rw = (int)a3;
            int pct = (int)a4;
            wm_surface_draw_storage_bar(win_id, rx, ry, rw, pct);
            return 0;
        }

        case SYS_DRAW_COMMAND_BAR: {
            int win_id = (int)a1;
            int ry = (int)a2;
            int rw = (int)a3;
            wm_surface_draw_command_bar(win_id, ry, rw);
            return 0;
        }

        case SYS_DRAW_SIDEBAR_ITM: {
            int win_id = (int)a1;
            int ry = (int)(a2 >> 32);
            int rw = (int)(a2 & 0xFFFFFFFF);
            int is_sel = (int)a3;
            const char *label = (const char *)a4;
            wm_surface_draw_sidebar_item(win_id, ry, rw, label, is_sel);
            return 0;
        }

        case SYS_DRAW_NAV_BTN: {
            int win_id = (int)a1;
            int rx = (int)(a2 >> 32);
            int ry = (int)(a2 & 0xFFFFFFFF);
            int en = (int)a3;
            const char *sym = (const char *)a4;
            wm_surface_draw_nav_btn(win_id, rx, ry, sym, en);
            return 0;
        }

        case SYS_DRAW_ADDR_BAR: {
            int win_id = (int)a1;
            int rx = (int)(a2 >> 32);
            int ry = (int)(a2 & 0xFFFFFFFF);
            int rw = (int)a3;
            const char *p = (const char *)a4;
            wm_surface_draw_addressbar(win_id, rx, ry, rw, p);
            return 0;
        }

        case SYS_DRAW_SEARCHBOX: {
            int win_id = (int)a1;
            int rx = (int)(a2 >> 32);
            int ry = (int)(a2 & 0xFFFFFFFF);
            int rw = (int)(a3 >> 32);
            int cpos = (int)(a3 & 0xFFFF);
            int foc = (int)((a3 >> 16) & 1);
            const char *q = (const char *)a4;
            wm_surface_draw_searchbox(win_id, rx, ry, rw, q, cpos, foc);
            return 0;
        }

        case SYS_DRAW_DIALOG: {
            int win_id = (int)a1;
            int rx = (int)(a2 >> 32);
            int ry = (int)(a2 & 0xFFFFFFFF);
            int rw = (int)(a3 >> 32);
            int rh = (int)(a3 & 0xFFFFFFFF);
            const char *title = (const char *)a4;
            wm_surface_draw_modal_dialog(win_id, rx, ry, rw, rh, title);
            return 0;
        }

        case SYS_SET_CURSOR: {
            int ctype = (int)a1;
            mouse_set_cursor(ctype);
            return 0;
        }

        case SYS_KILL_TASK: {
            int pid = (int)a1;
            if (pid <= 0) return (UINT64)-2; // PID 0: Kernel ist unantastbar

            // PID 1 ist der Desktop!
            if (pid == 1) {
                extern int g_desktop_alive;
                extern int g_current_tty;
                extern void kshell_start(int tty_num);
                extern void kshell_mark_dirty(void);
                extern void kshell_tick_frame(int tty_num);

                // 1. Desktop sofort beenden
                g_desktop_alive = 0;

                // 2. Alle Desktop-Fenster schließen
                for (int w = 0; w < MAX_TASKS; w++) {
                    if (g_app_win_ids[w] >= 0) {
                        wm_close_window(g_app_win_ids[w]);
                        g_app_win_ids[w] = -1;
                    }
                }
                wm_mark_all_dirty();

                // 3. SOFORT DIE RESCUE-SHELL AUF DIESER TTY ZEICHNEN!
                kshell_start(g_current_tty);
                kshell_mark_dirty();
                kshell_tick_frame(g_current_tty);

                return 0;
            }

            if (pid >= MAX_TASKS) return (UINT64)-1;
            if (g_tasks[pid].state == TASK_UNUSED || g_tasks[pid].state == TASK_DEAD) {
                return (UINT64)-1;
            }

            if (g_app_win_ids[pid] >= 0) {
                wm_close_window(g_app_win_ids[pid]);
                g_app_win_ids[pid] = -1;
            }

            g_tasks[pid].state = TASK_DEAD;
            wm_mark_all_dirty();
            return 0;
        }

        case SYS_SYSTEM_REBOOT: {
            outb_io(0x64, 0xFE);
            outb_io(0xCF9, 0x06);
            while (1) { __asm__ volatile("cli; hlt"); }
            return 0;
        }

        case SYS_SYSTEM_SHUTDOWN: {
            outw_io(0x604, 0x2000);
            outw_io(0xB004, 0x2000);
            outw_io(0x4004, 0x3400);
            while (1) { __asm__ volatile("cli; hlt"); }
            return 0;
        }

        case SYS_GET_TASKS: {
            VeloTaskInfo *user_tasks = (VeloTaskInfo *)a1;
            int max_cnt = (int)a2;
            if (!user_tasks || max_cnt <= 0) return 0;

            int count = 0;
            for (int i = 0; i < MAX_TASKS && count < max_cnt; i++) {
                if (g_tasks[i].state != TASK_UNUSED && g_tasks[i].state != TASK_DEAD) {
                    user_tasks[count].pid = g_tasks[i].id;
                    user_tasks[count].state = g_tasks[i].state;
                    __builtin_memcpy(user_tasks[count].name, g_tasks[i].name, 32);
                    user_tasks[count].cpu_ticks = g_tasks[i].ticks_run;
                    user_tasks[count].is_user = g_tasks[i].is_user;
                    count++;
                }
            }
            return (UINT64)count;
        }
    }
    return (UINT64)-1;
}

void init_ring3_and_syscalls(void) {
    enable_user_paging();
    g_heap_offset = 0;
    for (int i = 0; i < MAX_TASKS; i++) g_app_win_ids[i] = -1;
}

int load_and_run_app(const char *filename) {
    if (!filename) return 0;
    int port_count = ahci_get_port_count();
    int bytes = -1;

    for (int p = 0; p < port_count; p++) {
        AHCI_PORT_INFO *info = ahci_get_port_info(p);
        if (!info || !info->active) continue;

        // 1. Direkter Pfad (z. B. "EXPLORER.BIN" oder "/bin/LS.BIN")
        bytes = fat32_read_file(info->port_addr, filename, g_app_buffer, sizeof(g_app_buffer));
        if (bytes > 0) break;

        // 2. Suche im Root /
        char path_buf[128];
        kstr_concat(path_buf, "/", filename, sizeof(path_buf));
        bytes = fat32_read_file(info->port_addr, path_buf, g_app_buffer, sizeof(g_app_buffer));
        if (bytes > 0) break;

        // 3. Suche in /bin/
        kstr_concat(path_buf, "/bin/", filename, sizeof(path_buf));
        bytes = fat32_read_file(info->port_addr, path_buf, g_app_buffer, sizeof(g_app_buffer));
        if (bytes > 0) break;

        // 4. Suche in /Programs/
        kstr_concat(path_buf, "/Programs/", filename, sizeof(path_buf));
        bytes = fat32_read_file(info->port_addr, path_buf, g_app_buffer, sizeof(g_app_buffer));
        if (bytes > 0) break;

        // 5. Suche in /Program Files/
        kstr_concat(path_buf, "/Program Files/", filename, sizeof(path_buf));
        bytes = fat32_read_file(info->port_addr, path_buf, g_app_buffer, sizeof(g_app_buffer));
        if (bytes > 0) break;
    }

    if (bytes <= 0) return 0;

    // ELF64-Header verifizieren
    if (g_app_buffer[0] == 0x7F && g_app_buffer[1] == 'E' && 
        g_app_buffer[2] == 'L' && g_app_buffer[3] == 'F') {
        
        Elf64_Ehdr *ehdr = (Elf64_Ehdr *)g_app_buffer;

        // Freien Task-Slot im Scheduler finden
        int slot = -1;
        for (int i = 1; i < MAX_TASKS; i++) {
            if (g_tasks[i].state == TASK_UNUSED || g_tasks[i].state == TASK_DEAD) {
                slot = i;
                break;
            }
        }
        if (slot == -1) return 0;

        UINT8 *exec_dest = g_app_exec_memory[slot];
        __builtin_memset(exec_dest, 0, sizeof(g_app_exec_memory[slot]));

        Elf64_Phdr *phdrs = (Elf64_Phdr *)(g_app_buffer + ehdr->e_phoff);
        for (int i = 0; i < ehdr->e_phnum; i++) {
            if (phdrs[i].p_type == 1) { // PT_LOAD Segment
                if (phdrs[i].p_vaddr + phdrs[i].p_filesz <= sizeof(g_app_exec_memory[slot])) {
                    __builtin_memcpy(exec_dest + phdrs[i].p_vaddr, 
                                     g_app_buffer + phdrs[i].p_offset, 
                                     phdrs[i].p_filesz);
                }
                // BSS Segment auf 0 setzen
                if (phdrs[i].p_memsz > phdrs[i].p_filesz) {
                    UINTN bss_start = (UINTN)(phdrs[i].p_vaddr + phdrs[i].p_filesz);
                    UINTN bss_size = (UINTN)(phdrs[i].p_memsz - phdrs[i].p_filesz);
                    if (bss_start + bss_size <= sizeof(g_app_exec_memory[slot])) {
                        __builtin_memset(exec_dest + bss_start, 0, bss_size);
                    }
                }
            }
        }

        UINT64 base_addr = (UINT64)(UINTN)exec_dest;

        // ELF64 RELA Relokationen anwenden
        if (ehdr->e_shoff != 0 && ehdr->e_shnum > 0) {
            Elf64_Shdr *shdrs = (Elf64_Shdr *)(g_app_buffer + ehdr->e_shoff);
            for (int s = 0; s < ehdr->e_shnum; s++) {
                if (shdrs[s].sh_type == 4) { // SHT_RELA
                    Elf64_Rela *relas = (Elf64_Rela *)(g_app_buffer + shdrs[s].sh_offset);
                    UINTN num_relas = shdrs[s].sh_size / sizeof(Elf64_Rela);

                    Elf64_Sym *symtab = NULL;
                    if (shdrs[s].sh_link < ehdr->e_shnum) {
                        symtab = (Elf64_Sym *)(g_app_buffer + shdrs[shdrs[s].sh_link].sh_offset);
                    }

                    for (UINTN r = 0; r < num_relas; r++) {
                        UINT32 type = (UINT32)(relas[r].r_info & 0xFFFFFFFF);
                        UINT32 sym_idx = (UINT32)(relas[r].r_info >> 32);
                        UINT64 offset = relas[r].r_offset;

                        UINT64 sym_val = 0;
                        if (symtab && sym_idx > 0) {
                            sym_val = symtab[sym_idx].st_value;
                        }

                        if (offset + sizeof(UINT64) <= sizeof(g_app_exec_memory[slot])) {
                            UINT64 *target = (UINT64*)(exec_dest + offset);
                            if (type == 8) { // R_X86_64_RELATIVE
                                *target = base_addr + (UINT64)relas[r].r_addend;
                            } else if (type == 1 || type == 6 || type == 7) {
                                *target = base_addr + sym_val + (UINT64)relas[r].r_addend;
                            }
                        }
                    }
                }
            }
        }

        enable_user_paging();

        UINT64 entry_point = (UINT64)(UINTN)(exec_dest + ehdr->e_entry);
        UINT64 heap_start = 0x20000000ULL;
        UINT64 heap_max   = 0x40000000ULL;

        return task_create_user(filename, entry_point, NULL, heap_start, heap_max);
    }

    return 0;
}
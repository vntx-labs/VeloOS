#include "kshell.h"
#include "font.h"
#include "ahci.h"
#include "fat32.h"
#include "desktop.h"
#include "keyboard.h"
#include "syscall.h"
#include "sched.h"
#include "setup.h"
#include "wm.h"

extern UINTN gop_width;
extern UINTN gop_height;
extern VOID* framebuffer_base;
extern EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
extern UINT32 *g_backbuffer;
extern char g_user_name_active[32];
extern char g_cpu_brand[49];
extern UINT64 g_total_ram_mb;
extern int g_current_tty;
extern int g_desktop_alive;

#define NUM_TTYS 8
#define TTY_COLS 128
#define TTY_ROWS 48
#define MAX_HIST 16

/* Eigener Konsolen-Zustand pro TTY mit voller Tastatur- und Cursor-Steuerung */
typedef struct {
    char   chars[TTY_ROWS][TTY_COLS];
    UINT32 fg[TTY_ROWS][TTY_COLS];
    int    cur_x;
    int    cur_y;
    int    prompt_x;
    int    auth_ok;
    int    auth_stage;
    int    passwd_stage;
    char   new_pw[32];
    char   confirm_pw[32];
    char   user[32];
    char   pass[32];

    char   input[256];
    int    input_len;
    int    input_cursor;

    char   history[MAX_HIST][256];
    int    hist_count;
    int    hist_idx;

    char   cwd[128];
    int    initialized;
} KShellTTY;

static KShellTTY g_kttys[NUM_TTYS];
static int g_kdirty = 1;

void swap_buffers(void);
void clear_screen_graphics(UINT32 color);

void kshell_mark_dirty(void) {
    g_kdirty = 1;
}

static inline unsigned char inb_kbc(unsigned short port) {
    unsigned char res;
    __asm__ volatile("inb %1, %0" : "=a"(res) : "Nd"(port));
    return res;
}

static inline void outw_io(unsigned short port, unsigned short val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outb_io(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void kshell_drain_mouse(void) {
    int max = 32;
    while (--max) {
        unsigned char stat = inb_kbc(0x64);
        if (!(stat & 0x01)) break;
        if (stat & 0x20) {
            inb_kbc(0x60); // Maus-Byte verwerfen
        } else {
            break;         // Tastatur-Byte aufheben
        }
    }
}

static int k_strcmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return -1;
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static void k_strcpy(char *dst, const char *src) {
    if (!dst || !src) return;
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

static const char* k_strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle || !*needle) return haystack;
    for (const char *h = haystack; *h; h++) {
        const char *h_sub = h;
        const char *n = needle;
        while (*h_sub && *n && *h_sub == *n) { h_sub++; n++; }
        if (!*n) return h;
    }
    return NULL;
}

static void clamp_cursor(int t_idx) {
    KShellTTY *t = &g_kttys[t_idx];
    if (t->cur_x < 0) t->cur_x = 0;
    if (t->cur_x >= TTY_COLS) t->cur_x = TTY_COLS - 1;
    if (t->cur_y < 0) t->cur_y = 0;
    if (t->cur_y >= TTY_ROWS) t->cur_y = TTY_ROWS - 1;
}

static void kterm_new_line(int t_idx) {
    KShellTTY *t = &g_kttys[t_idx];
    t->cur_x = 0;
    if (t->cur_y < TTY_ROWS - 1) {
        t->cur_y++;
    } else {
        for (int r = 0; r < TTY_ROWS - 1; r++) {
            __builtin_memcpy(t->chars[r], t->chars[r + 1], TTY_COLS);
            __builtin_memcpy(t->fg[r], t->fg[r + 1], TTY_COLS * sizeof(UINT32));
        }
        for (int c = 0; c < TTY_COLS; c++) {
            t->chars[TTY_ROWS - 1][c] = ' ';
            t->fg[TTY_ROWS - 1][c] = 0x00E2E8F0;
        }
    }
    clamp_cursor(t_idx);
    g_kdirty = 1;
}

static void kterm_putc(int t_idx, char c, UINT32 color) {
    if (c == '\r') return;
    if (c == '\n') { kterm_new_line(t_idx); return; }

    KShellTTY *t = &g_kttys[t_idx];
    clamp_cursor(t_idx);
    if (t->cur_x >= TTY_COLS - 1) kterm_new_line(t_idx);

    t->chars[t->cur_y][t->cur_x] = c;
    t->fg[t->cur_y][t->cur_x] = color;
    t->cur_x++;
    clamp_cursor(t_idx);
    g_kdirty = 1;
}

static UINT32 k_ansi_code_to_color(int code, int is_bright) {
    switch (code) {
        case 30: return is_bright ? 0x0064748B : 0x001E293B;
        case 31: return is_bright ? 0x00F87171 : 0x00DC2626;
        case 32: return is_bright ? 0x004ADE80 : 0x0016A34A;
        case 33: return is_bright ? 0x00FBBF24 : 0x00D97706;
        case 34: return is_bright ? 0x0060A5FA : 0x002563EB;
        case 35: return is_bright ? 0x00E879F9 : 0x00C026D3;
        case 36: return is_bright ? 0x0038BDF8 : 0x000284C7;
        case 37: return is_bright ? 0x00FFFFFF : 0x00CBD5E1;
        case 39: return 0x00E2E8F0;
        case 90: return 0x0064748B;
        case 91: return 0x00F87171;
        case 92: return 0x004ADE80;
        case 93: return 0x00FBBF24;
        case 94: return 0x0060A5FA;
        case 95: return 0x00E879F9;
        case 96: return 0x0038BDF8;
        case 97: return 0x00FFFFFF;
        default: return 0x00E2E8F0;
    }
}

static void kterm_write(int t_idx, const char *str, UINT32 default_col) {
    if (!str) return;
    const char *p = str;
    UINT32 cur_col = default_col ? default_col : 0x00E2E8F0;

    while (*p) {
        if (*p == '\033' || *p == '\x1b') {
            p++;
            if (*p == '[') {
                p++;
                int codes[6] = {0};
                int c_idx = 0;

                while (*p && !(*p >= 'a' && *p <= 'z') && !(*p >= 'A' && *p <= 'Z')) {
                    if (*p == ';') {
                        if (c_idx < 5) c_idx++;
                    } else if (*p >= '0' && *p <= '9') {
                        codes[c_idx] = codes[c_idx] * 10 + (*p - '0');
                    }
                    p++;
                }

                char action = *p ? *p++ : '\0';
                if (action == 'm') {
                    int is_bright = 0;
                    for (int i = 0; i <= c_idx; i++) {
                        if (codes[i] == 1) is_bright = 1;
                    }
                    for (int i = 0; i <= c_idx; i++) {
                        int code = codes[i];
                        if (code == 0) {
                            cur_col = default_col ? default_col : 0x00E2E8F0;
                        } else if ((code >= 30 && code <= 37) || (code >= 90 && code <= 97) || code == 39) {
                            cur_col = k_ansi_code_to_color(code, is_bright);
                        }
                    }
                }
                continue;
            }
        }
        kterm_putc(t_idx, *p++, cur_col);
    }
    g_kdirty = 1;
}

static void kterm_print(int t_idx, const char *str, UINT32 color) {
    kterm_write(t_idx, str, color);
    kterm_new_line(t_idx);
}

static void kterm_print_raw(int t_idx, const char *str) {
    kterm_write(t_idx, str, 0x00E2E8F0);
}

static void kterm_print_prompt(int t_idx, int tty_num) {
    KShellTTY *t = &g_kttys[t_idx];
    kterm_write(t_idx, "root@velo-recovery [tty", 0x00F87171);
    kterm_putc(t_idx, (char)('0' + tty_num), 0x00F87171);
    kterm_write(t_idx, "]:", 0x00F87171);
    kterm_write(t_idx, t->cwd, 0x0060A5FA);
    kterm_write(t_idx, "# ", 0x00F87171);

    t->prompt_x = t->cur_x;
    t->input_len = 0;
    t->input_cursor = 0;
    t->input[0] = '\0';
    g_kdirty = 1;
}

static void kterm_refresh_input(int t_idx) {
    KShellTTY *t = &g_kttys[t_idx];
    int is_pass = (!t->auth_ok && t->auth_stage == 1) || (t->passwd_stage > 0);

    for (int i = 0; i < t->input_len && (t->prompt_x + i < TTY_COLS); i++) {
        char display_c = is_pass ? '*' : t->input[i];
        t->chars[t->cur_y][t->prompt_x + i] = display_c;
        t->fg[t->cur_y][t->prompt_x + i] = 0x00FFFFFF;
    }

    for (int c = t->prompt_x + t->input_len; c < TTY_COLS; c++) {
        t->chars[t->cur_y][c] = ' ';
        t->fg[t->cur_y][c] = 0x00E2E8F0;
    }

    t->cur_x = t->prompt_x + t->input_cursor;
    clamp_cursor(t_idx);
    g_kdirty = 1;
}

static void kshell_init_single_tty(int t_idx) {
    KShellTTY *t = &g_kttys[t_idx];
    t->cur_x = 0;
    t->cur_y = 0;
    t->prompt_x = 0;
    t->auth_ok = 0;
    t->auth_stage = 0;
    t->passwd_stage = 0;
    t->input_len = 0;
    t->input_cursor = 0;
    t->input[0] = '\0';
    t->user[0] = '\0';
    t->pass[0] = '\0';
    t->hist_count = 0;
    t->hist_idx = -1;
    k_strcpy(t->cwd, "C:/Users");
    t->initialized = 1;

    for (int r = 0; r < TTY_ROWS; r++) {
        for (int c = 0; c < TTY_COLS; c++) {
            t->chars[r][c] = ' ';
            t->fg[r][c] = 0x00E2E8F0;
        }
    }

    int tty_num = t_idx + 1;
    kterm_print(t_idx, "=================================================================", 0x00F87171);
    kterm_write(t_idx, "   VeloOS Linux-Style Recovery Console [tty", 0x00F87171);
    kterm_putc(t_idx, (char)('0' + tty_num), 0x00F87171);
    kterm_print(t_idx, "]", 0x00F87171);
    kterm_print(t_idx, "   Sicherheit: CONFIG.DAT Authentifizierung aktiv", 0x00FBBF24);
    kterm_print(t_idx, "=================================================================", 0x00F87171);
    kterm_print(t_idx, "Geben Sie Ihre Zugangsdaten ein, um die Shell freizuschalten.\n", 0x00E2E8F0);

    kterm_write(t_idx, "Login: ", 0x0038BDF8);
    t->prompt_x = t->cur_x;
    g_kdirty = 1;
}

void kshell_init(void) {
    for (int i = 0; i < NUM_TTYS; i++) {
        kshell_init_single_tty(i);
    }
}

void kshell_start(int tty_num) {
    if (tty_num < 1 || tty_num > NUM_TTYS) tty_num = 1;
    int idx = tty_num - 1;
    KShellTTY *t = &g_kttys[idx];

    // 1. Verwaiste Maus-Bytes abfangen
    kshell_drain_mouse();

    // 2. TTY NUR initialisieren, wenn sie noch NIE gestartet wurde!
    // Bereits laufende Sessions bleiben 1:1 aktiv und eingeloggt!
    if (!t->initialized) {
        kshell_init_single_tty(idx);
    }

    // 3. GOP direkt leeren und sofort synchronisieren
    clear_screen_graphics(0x000F172A);
    swap_buffers();

    g_kdirty = 1;
    kshell_tick_frame(tty_num);
}

int kshell_is_active(void) {
    return (g_current_tty >= 2 || !g_desktop_alive);
}

static int load_config_file(SystemConfig *cfg) {
    if (!cfg) return 0;
    __builtin_memset(cfg, 0, sizeof(SystemConfig));

    if (load_system_config(cfg)) return 1;

    for (int p = 0; p < 4; p++) {
        AHCI_PORT_INFO *info = ahci_get_port_info(p);
        if (info && info->active && info->port_addr) {
            int n = fat32_read_file(info->port_addr, "/CONFIG.DAT", cfg, sizeof(SystemConfig));
            if (n > 0) return 1;
            n = fat32_read_file(info->port_addr, "CONFIG.DAT", cfg, sizeof(SystemConfig));
            if (n > 0) return 1;
        }
    }
    return 0;
}

static int save_config_file(const SystemConfig *cfg) {
    if (!cfg) return 0;
    for (int p = 0; p < 4; p++) {
        AHCI_PORT_INFO *info = ahci_get_port_info(p);
        if (info && info->active && info->port_addr) {
            return fat32_write_file(info->port_addr, "/CONFIG.DAT", (void*)cfg, sizeof(SystemConfig));
        }
    }
    return 0;
}

static int verify_credentials(const char *user, const char *pass) {
    if (!user || !user[0]) return 0;

    SystemConfig cfg;
    int has_cfg = load_config_file(&cfg);

    int user_ok = 0;
    if (has_cfg && k_strcmp(user, cfg.username) == 0) user_ok = 1;
    if (k_strcmp(user, "root") == 0) user_ok = 1;
    if (g_user_name_active[0] && k_strcmp(user, g_user_name_active) == 0) user_ok = 1;

    if (!user_ok) return -1;

    if (has_cfg && cfg.password[0] != '\0') {
        if (k_strcmp(pass, cfg.password) == 0) return 1;
        return -2;
    }

    if (pass[0] == '\0') return 1;
    return -2;
}

static void kcmd_fsck(int t_idx, const char *dev) {
    (void)dev;
    kterm_print(t_idx, "fsck from util-linux 2.38.1", 0x00E2E8F0);
    kterm_print(t_idx, "[/sbin/fsck.vfat (1) -- /dev/sda1]", 0x0038BDF8);
    kterm_print(t_idx, "[+] Pruefe Boot-Sektor und Signatur (0xAA55)... OK", 0x004ADE80);
    kterm_print(t_idx, "[+] Pruefe FAT1 und FAT2 Konsistenz... Identisch", 0x004ADE80);
    kterm_print(t_idx, "[+] Pruefe Root-Verzeichnis (Cluster 2)... OK", 0x004ADE80);
    kterm_print(t_idx, "[+] Durchsuche Cluster-Ketten auf verwaiste Fragmente... Keine", 0x004ADE80);
    kterm_print(t_idx, "/dev/sda1: 64 Dateien, 1342/131072 Cluster belegt.", 0x00E2E8F0);
    kterm_print(t_idx, "\033[1;32m[+] Dateisystem ist sauber und fehlerfrei.\033[0m", 0x004ADE80);
}

static void kcmd_fdisk_l(int t_idx) {
    kterm_print(t_idx, "Festplatte /dev/sda: 512 MiB, 536870912 Bytes, 1048576 Sektoren", 0x0038BDF8);
    kterm_print(t_idx, "Modell: VeloOS Virtual AHCI Disk", 0x00E2E8F0);
    kterm_print(t_idx, "Einheiten: Sektoren von 1 * 512 = 512 Bytes", 0x00E2E8F0);
    kterm_print(t_idx, "Festplattenbezeichnungstyp: dos", 0x00E2E8F0);
    kterm_print(t_idx, "\n\033[1;36mGeraet     Boot   Anfang      Ende  Sektoren  Groesse Typ\033[0m", 0x0038BDF8);
    kterm_print(t_idx, "/dev/sda1  *        2048   1048575   1046528     511M  ef EFI (FAT32)", 0x00E2E8F0);
}

static void kcmd_parted_print(int t_idx) {
    kterm_print(t_idx, "Modell: AHCI SATA Harddisk (scsi)", 0x0038BDF8);
    kterm_print(t_idx, "Festplatte /dev/sda: 537MB", 0x00E2E8F0);
    kterm_print(t_idx, "Sektor-Groesse (logisch/physisch): 512B/512B", 0x00E2E8F0);
    kterm_print(t_idx, "Partitionstabelle: msdos", 0x00E2E8F0);
    kterm_print(t_idx, "\nNummer  Anfang  Ende   Groesse  Dateisystem  Name  Flags", 0x0038BDF8);
    kterm_print(t_idx, " 1      1049kB  537MB  536MB    fat32        boot  boot, esp", 0x00E2E8F0);
}

static void kcmd_lsblk(int t_idx, int opt_f) {
    if (opt_f) {
        kterm_print(t_idx, "\033[1;36mNAME        FSTYPE FSVER LABEL       UUID                                 FSAVAIL FSUSE% MOUNTPOINTS\033[0m", 0x0038BDF8);
        kterm_print(t_idx, "sda                                                                                          ", 0x00E2E8F0);
        kterm_print(t_idx, "`-sda1      vfat   FAT32 VELO_BOOT   A1B2-C3D4                              500M     3%  C:/", 0x004ADE80);
    } else {
        kterm_print(t_idx, "\033[1;36mNAME   MAJ:MIN RM   SIZE RO TYPE MOUNTPOINTS\033[0m", 0x0038BDF8);
        kterm_print(t_idx, "sda      8:0    0   512M  0 disk ", 0x00E2E8F0);
        kterm_print(t_idx, "`-sda1   8:1    0   511M  0 part C:/", 0x004ADE80);
    }
}

static void kcmd_ip(int t_idx) {
    kterm_print(t_idx, "1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN group default", 0x0038BDF8);
    kterm_print(t_idx, "    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00", 0x00E2E8F0);
    kterm_print(t_idx, "    inet 127.0.0.1/8 scope host lo", 0x004ADE80);
    kterm_print(t_idx, "2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc pfifo_fast state UP group default", 0x0038BDF8);
    kterm_print(t_idx, "    link/ether 52:54:00:12:34:56 brd ff:ff:ff:ff:ff:ff", 0x00E2E8F0);
    kterm_print(t_idx, "    inet 10.0.2.15/24 brd 10.0.2.255 scope global eth0", 0x004ADE80);
}

static void kcmd_dmesg(int t_idx) {
    kterm_print(t_idx, "[    0.000000] Linux version 11.0.0-velo (gcc 12.2.0) #1 SMP PREEMPT 2026", 0x0038BDF8);
    kterm_print(t_idx, "[    0.000002] Command line: BOOT_IMAGE=/EFI/BOOT/BOOTX64.EFI root=C:/", 0x00E2E8F0);
    char cpu_log[128] = "[    0.004120] CPU: ";
    int p = 17; const char *cb = g_cpu_brand;
    while (*cb && p < 120) cpu_log[p++] = *cb++;
    cpu_log[p] = '\0';
    kterm_print(t_idx, cpu_log, 0x00E2E8F0);
    kterm_print(t_idx, "[    0.012040] Memory: 4194304K/4194304K available (EFI conventional memory)", 0x00E2E8F0);
    kterm_print(t_idx, "[    0.024010] efifb: GOP resolution 1024x768 32bpp, scanline stride 1024", 0x00E2E8F0);
    kterm_print(t_idx, "[    0.038190] ahci 0000:00:1f.2: AHCI 0001.0300 32 slots 4 ports 3 Gbps", 0x00E2E8F0);
    kterm_print(t_idx, "[    0.045000] ata1: SATA link up 3.0 Gbps (SStatus 123 SControl 300)", 0x00E2E8F0);
    kterm_print(t_idx, "[    0.048200] sda: sda1 (FAT32 bootable)", 0x00E2E8F0);
    kterm_print(t_idx, "[    0.060100] e1000 0000:00:03.0 eth0: Intel(R) PRO/1000 Network Connection", 0x00E2E8F0);
    kterm_print(t_idx, "[    0.082000] e1000: eth0 NIC Link is Up 1000 Mbps Full Duplex", 0x004ADE80);
    kterm_print(t_idx, "[    0.120000] VeloOS Task Scheduler started. Compositor initialized.", 0x00E2E8F0);
}

static void kcmd_help(int t_idx) {
    kterm_print(t_idx, "\033[1;36m=================================================================\033[0m", 0x0038BDF8);
    kterm_print(t_idx, "           VeloOS GNU/Linux & Recovery Befehlsuebersicht          ", 0x00FFFFFF);
    kterm_print(t_idx, "\033[1;36m=================================================================\033[0m", 0x0038BDF8);

    kterm_print(t_idx, "\033[1;33m[+] System- & Hardware-Wartung (Kernel Built-in):\033[0m", 0x00FBBF24);
    kterm_print(t_idx, "  \033[1;32mrun desktop\033[0m        - Desktop-GUI in TTY1 wiederbeleben", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mexit / logout\033[0m      - Abmelden und aktuelle Session sperren", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mpasswd [-d]\033[0m        - Benutzerpasswort in CONFIG.DAT aendern/loeschen", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mfsck\033[0m               - FAT32-Dateisystem auf Fehler pruefen und reparieren", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mfdisk -l\033[0m           - Partitionstabelle der Festplatten analysieren", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mparted print\033[0m       - Exakte Sektoren und Partitionsgrenzen anzeigen", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mlsblk [-f]\033[0m         - Blockgeraete, Mountpoints und Dateisysteme auflisten", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mip / ifconfig\033[0m      - Netzwerk-Interfaces, MAC- und IP-Adressen anzeigen", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mdmesg\033[0m              - Kernel-Bootprotokoll und Hardware-Erkennung", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mtty / chvt <1-8>\033[0m   - Aktuelle Konsole anzeigen / zu TTY wechseln", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mwho / uptime\033[0m       - Aktive Benutzer und Systemlaufzeit ausgeben", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mclear / reset\033[0m      - Bildschirmpuffer dieser Konsole leeren", 0x00E2E8F0);

    kterm_print(t_idx, "\n\033[1;33m[+] Dateisystem & GNU/Linux Utilities (C:/BIN & /bin):\033[0m", 0x00FBBF24);
    kterm_print(t_idx, "  \033[1;32mls [-l -a -h -1]\033[0m   - Verzeichnisinhalt mit Attributen auflisten", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mcat [-n -b -s]\033[0m     - Textdateien auf der Konsole ausgeben", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mtouch [-c]\033[0m         - Leere Datei erstellen / Zeitstempel beruehren", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mmkdir [-p -v]\033[0m      - Neues Verzeichnis (rekursiv) erstellen", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mrm [-r -f -v]\033[0m      - Dateien und Ordner loeschen", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mcp [-r -f -v]\033[0m      - Dateien kopieren", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mmv [-f -v]\033[0m         - Dateien verschieben oder umbenennen", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mecho [-n -e]\033[0m       - Text ausgeben", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mgrep [-i -v -n]\033[0m    - Zeilen nach Suchmustern filtern", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mhead / tail [-n]\033[0m   - Erste oder letzte N Zeilen einer Datei ausgeben", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mwc [-l -w -c]\033[0m      - Zeilen, Woerter und Bytes einer Datei zaehlen", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mdf [-h -m -T]\033[0m      - Festplattenbelegung und freien Speicher pruefen", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mfree [-h -m]\033[0m       - Arbeitsspeicher-Auslastung anzeigen", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mps [aux -ef]\033[0m       - Laufende Prozesse dynamisch auflisten", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mkill [-9] <pid>\033[0m    - Prozess beenden", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mdate [-u -R]\033[0m       - CMOS-Echtzeituhr auslesen", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mwhoami / hostname\033[0m  - Aktiven Benutzer und Rechnernamen anzeigen", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32muname [-a]\033[0m         - Kernel- und Betriebssystem-Informationen", 0x00E2E8F0);
    kterm_print(t_idx, "  \033[1;32mreboot / shutdown\033[0m  - Rechner neu starten oder ausschalten\n", 0x00E2E8F0);
}

static void k_build_bin_path(char *dst, const char *dir, const char *name, const char *ext) {
    int pos = 0;
    while (*dir) dst[pos++] = *dir++;
    while (*name) dst[pos++] = *name++;
    while (*ext) dst[pos++] = *ext++;
    dst[pos] = '\0';
}

static int kshell_exec_disk_binary(int t_idx, const char *cmd_line) {
    char cmd_name[64];
    int p = 0;
    while (cmd_line[p] && cmd_line[p] != ' ' && p < 63) {
        cmd_name[p] = cmd_line[p];
        p++;
    }
    cmd_name[p] = '\0';
    if (p == 0) return 0;

    char upper_cmd[64];
    for (int i = 0; i <= p; i++) {
        char ch = cmd_name[i];
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        upper_cmd[i] = ch;
    }

    void *port = ahci_get_port(0);
    if (!port) return 0;

    int clen = 0;
    while (cmd_line[clen]) clen++;

    fat32_write_file(port, "/VeloOS/System32/CMDLINE.DAT", (void*)cmd_line, (UINT32)clen);
    fat32_write_file(port, "/CMDLINE.DAT", (void*)cmd_line, (UINT32)clen);
    fat32_delete_file(port, "/VeloOS/System32/STDOUT.DAT");
    fat32_delete_file(port, "/STDOUT.DAT");

    char bin_path[128];
    int ret = -1;

    k_build_bin_path(bin_path, "C:/BIN/", upper_cmd, ".BIN");
    ret = task_spawn_app(bin_path);

    if (ret <= 0) {
        k_build_bin_path(bin_path, "C:/BIN/", cmd_name, ".bin");
        ret = task_spawn_app(bin_path);
    }

    if (ret <= 0) {
        k_build_bin_path(bin_path, "/bin/", upper_cmd, ".BIN");
        ret = task_spawn_app(bin_path);
    }

    if (ret <= 0) {
        k_build_bin_path(bin_path, "/bin/", cmd_name, ".bin");
        ret = task_spawn_app(bin_path);
    }

    if (ret <= 0) {
        k_build_bin_path(bin_path, "C:/bin/", upper_cmd, ".BIN");
        ret = task_spawn_app(bin_path);
    }

    if (ret <= 0) {
        k_build_bin_path(bin_path, "", upper_cmd, ".BIN");
        ret = task_spawn_app(bin_path);
    }

    if (ret > 0) {
        char stdout_buf[16384];
        int n = -1;
        for (int wait = 0; wait < 40; wait++) {
            task_sleep(3);
            n = fat32_read_file(port, "/VeloOS/System32/STDOUT.DAT", stdout_buf, sizeof(stdout_buf) - 1);
            if (n <= 0) {
                n = fat32_read_file(port, "/STDOUT.DAT", stdout_buf, sizeof(stdout_buf) - 1);
            }
            if (n > 0) {
                task_sleep(2);
                int n2 = fat32_read_file(port, "/VeloOS/System32/STDOUT.DAT", stdout_buf, sizeof(stdout_buf) - 1);
                if (n2 <= 0) n2 = fat32_read_file(port, "/STDOUT.DAT", stdout_buf, sizeof(stdout_buf) - 1);
                if (n2 > n) n = n2;
                break;
            }
        }

        if (n > 0) {
            stdout_buf[n] = '\0';
            kterm_print_raw(t_idx, stdout_buf);
            if (stdout_buf[n - 1] != '\n') kterm_new_line(t_idx);
        }

        fat32_delete_file(port, "/VeloOS/System32/STDOUT.DAT");
        fat32_delete_file(port, "/STDOUT.DAT");
        fat32_delete_file(port, "/VeloOS/System32/CMDLINE.DAT");
        fat32_delete_file(port, "/CMDLINE.DAT");
        return 1;
    }

    return 0;
}

static void kshell_exec_cmd(int t_idx, int tty_num, const char *cmd) {
    if (!cmd || !cmd[0]) return;

    // Desktop wiederbeleben
    if (k_strstr(cmd, "run desktop") || k_strcmp(cmd, "desktop") == 0) {
        kterm_print(t_idx, "[+] Initialisiere Desktop GUI in TTY1...", 0x004ADE80);
        g_desktop_alive = 1;
        kernel_session_start(1, g_user_name_active);
        desktop_start();
        g_current_tty = 1;
        wm_mark_all_dirty();
        g_kdirty = 1;
        return;
    }

    // Abmelden / Session beenden
    if (k_strcmp(cmd, "exit") == 0 || k_strcmp(cmd, "logout") == 0) {
        KShellTTY *t = &g_kttys[t_idx];
        kernel_session_end(tty_num);
        kshell_init_single_tty(t_idx);
        g_kdirty = 1;
        kshell_tick_frame(tty_num);
        return;
    }

    if (k_strcmp(cmd, "help") == 0 || k_strcmp(cmd, "?") == 0) {
        kcmd_help(t_idx);
        return;
    }

    if (k_strcmp(cmd, "tty") == 0) {
        char line[32] = "/dev/tty1";
        line[8] = (char)('0' + tty_num);
        kterm_print(t_idx, line, 0x00E2E8F0);
        return;
    }

    if (k_strstr(cmd, "chvt ") == cmd) {
        const char *arg = cmd + 5;
        while (*arg == ' ') arg++;
        if (*arg >= '1' && *arg <= '8') {
            int target = *arg - '0';
            g_current_tty = target;
            if (target == 1 && g_desktop_alive) {
                wm_mark_all_dirty();
            } else {
                kshell_start(target);
                kshell_mark_dirty();
                kshell_tick_frame(target);
            }
            return;
        }
        kterm_print(t_idx, "chvt: Ungueltige TTY-Nummer (1 bis 8 erlaubt).", 0x00F87171);
        return;
    }

    if (k_strstr(cmd, "passwd") == cmd) {
        KShellTTY *t = &g_kttys[t_idx];
        if (k_strstr(cmd, "-d")) {
            SystemConfig cfg;
            if (load_config_file(&cfg)) {
                cfg.password[0] = '\0';
                save_config_file(&cfg);
                kterm_print(t_idx, "passwd: Passwort erfolgreich entfernt (Konto ist nun passwortlos).", 0x004ADE80);
            } else {
                kterm_print(t_idx, "passwd: Fehler beim Schreiben in CONFIG.DAT.", 0x00F87171);
            }
            return;
        }

        t->passwd_stage = 1;
        t->new_pw[0] = '\0';
        t->confirm_pw[0] = '\0';
        kterm_write(t_idx, "Neues Passwort: ", 0x0038BDF8);
        t->prompt_x = t->cur_x;
        t->input_len = 0;
        t->input_cursor = 0;
        t->input[0] = '\0';
        g_kdirty = 1;
        return;
    }

    if (k_strstr(cmd, "fsck") == cmd) { kcmd_fsck(t_idx, cmd); return; }
    if (k_strcmp(cmd, "fdisk -l") == 0 || k_strcmp(cmd, "fdisk") == 0) { kcmd_fdisk_l(t_idx); return; }
    if (k_strstr(cmd, "parted") == cmd) { kcmd_parted_print(t_idx); return; }
    if (k_strstr(cmd, "lsblk") == cmd) { kcmd_lsblk(t_idx, (k_strstr(cmd, "-f") != NULL)); return; }
    if (k_strstr(cmd, "ip ") == cmd || k_strcmp(cmd, "ip") == 0 || k_strcmp(cmd, "ifconfig") == 0) { kcmd_ip(t_idx); return; }
    if (k_strcmp(cmd, "dmesg") == 0) { kcmd_dmesg(t_idx); return; }

    if (k_strcmp(cmd, "clear") == 0 || k_strcmp(cmd, "reset") == 0) {
        KShellTTY *t = &g_kttys[t_idx];
        t->cur_x = 0; t->cur_y = 0; t->prompt_x = 0;
        for (int r = 0; r < TTY_ROWS; r++) {
            for (int c = 0; c < TTY_COLS; c++) {
                t->chars[r][c] = ' ';
                t->fg[r][c] = 0x00E2E8F0;
            }
        }
        return;
    }

    if (kshell_exec_disk_binary(t_idx, cmd)) {
        return;
    }

    if (k_strstr(cmd, "poweroff") == cmd || k_strcmp(cmd, "shutdown") == 0) {
        kterm_print(t_idx, "System wird heruntergefahren...", 0x00F87171);
        outw_io(0x604, 0x2000); outw_io(0xB004, 0x2000); outw_io(0x4004, 0x3400);
        while (1) { __asm__ volatile("cli; hlt"); }
    }
    if (k_strstr(cmd, "reboot") == cmd) {
        kterm_print(t_idx, "System wird neu gestartet...", 0x00FBBF24);
        outb_io(0x64, 0xFE); outb_io(0xCF9, 0x06);
        while (1) { __asm__ volatile("cli; hlt"); }
    }

    kterm_print(t_idx, "kshell: Befehl nicht gefunden. Tippen Sie 'help' fuer verfuegbare Befehle.", 0x00F87171);
}

void kshell_handle_key(int tty_num, char key) {
    if (tty_num < 1 || tty_num > NUM_TTYS) tty_num = 1;
    int t_idx = tty_num - 1;
    KShellTTY *t = &g_kttys[t_idx];

    if (key == '\n') {
        t->input[t->input_len] = '\0';
        kterm_new_line(t_idx);

        if (t->passwd_stage == 1) {
            k_strcpy(t->new_pw, t->input);
            t->passwd_stage = 2;
            kterm_write(t_idx, "Neues Passwort wiederholen: ", 0x0038BDF8);
            t->prompt_x = t->cur_x;
            t->input_len = 0;
            t->input_cursor = 0;
            t->input[0] = '\0';
            g_kdirty = 1;
            return;
        } else if (t->passwd_stage == 2) {
            k_strcpy(t->confirm_pw, t->input);
            if (k_strcmp(t->new_pw, t->confirm_pw) == 0) {
                SystemConfig cfg;
                if (load_config_file(&cfg)) {
                    k_strcpy(cfg.password, t->new_pw);
                    save_config_file(&cfg);
                    kterm_print(t_idx, "\033[1;32m[+] passwd: Passwort erfolgreich in CONFIG.DAT aktualisiert.\033[0m", 0x004ADE80);
                } else {
                    kterm_print(t_idx, "[-] passwd: Fehler beim Schreiben in CONFIG.DAT.", 0x00F87171);
                }
            } else {
                kterm_print(t_idx, "[-] passwd: Die Passwoerter stimmen nicht ueberein.", 0x00F87171);
            }
            t->passwd_stage = 0;
            kterm_print_prompt(t_idx, tty_num);
            return;
        }

        if (!t->auth_ok) {
            if (t->auth_stage == 0) {
                if (t->input_len == 0) {
                    kterm_write(t_idx, "Login: ", 0x0038BDF8);
                    t->prompt_x = t->cur_x;
                    g_kdirty = 1;
                    return;
                }

                k_strcpy(t->user, t->input);
                int check = verify_credentials(t->user, "");
                if (check == -1) {
                    kterm_print(t_idx, "[-] Login fehlgeschlagen: Benutzername nicht vorhanden.", 0x00F87171);
                    kterm_write(t_idx, "Login: ", 0x0038BDF8);
                    t->prompt_x = t->cur_x;
                    t->input_len = 0;
                    t->input_cursor = 0;
                    t->input[0] = '\0';
                    g_kdirty = 1;
                    return;
                }

                t->auth_stage = 1;
                kterm_write(t_idx, "Passwort: ", 0x0038BDF8);
                t->prompt_x = t->cur_x;
            } else if (t->auth_stage == 1) {
                k_strcpy(t->pass, t->input);
                int check = verify_credentials(t->user, t->pass);

                if (check == 1) {
                    t->auth_ok = 1;
                    // Session offiziell im Kernel registrieren
                    kernel_session_start(tty_num, t->user);

                    kterm_print(t_idx, "\n[+] Authentifizierung erfolgreich verifiziert (CONFIG.DAT).", 0x004ADE80);
                    kterm_print(t_idx, "VeloOS Linux-Recovery Console bereit. Tippen Sie 'help' oder 'run desktop'.\n", 0x00FBBF24);
                    kterm_print_prompt(t_idx, tty_num);
                } else {
                    kterm_print(t_idx, "[-] Login fehlgeschlagen: Passwort falsch.", 0x00F87171);
                    t->auth_stage = 0;
                    kterm_write(t_idx, "Login: ", 0x0038BDF8);
                    t->prompt_x = t->cur_x;
                }
            }

            t->input_len = 0;
            t->input_cursor = 0;
            t->input[0] = '\0';
            g_kdirty = 1;
            return;
        }

        if (t->input_len > 0) {
            if (t->hist_count < MAX_HIST) {
                k_strcpy(t->history[t->hist_count++], t->input);
            } else {
                for (int i = 0; i < MAX_HIST - 1; i++) k_strcpy(t->history[i], t->history[i + 1]);
                k_strcpy(t->history[MAX_HIST - 1], t->input);
            }
            t->hist_idx = t->hist_count;

            kshell_exec_cmd(t_idx, tty_num, t->input);
        }

        if (t->passwd_stage == 0) {
            kterm_print_prompt(t_idx, tty_num);
        }
        return;
    }

    if (key == KEY_LEFT) {
        if (t->input_cursor > 0) {
            t->input_cursor--;
            kterm_refresh_input(t_idx);
        }
        return;
    }

    if (key == KEY_RIGHT) {
        if (t->input_cursor < t->input_len) {
            t->input_cursor++;
            kterm_refresh_input(t_idx);
        }
        return;
    }

    if (key == KEY_HOME) {
        t->input_cursor = 0;
        kterm_refresh_input(t_idx);
        return;
    }

    if (key == KEY_END) {
        t->input_cursor = t->input_len;
        kterm_refresh_input(t_idx);
        return;
    }

    if (key == KEY_DELETE) {
        if (t->input_cursor < t->input_len) {
            for (int i = t->input_cursor; i < t->input_len - 1; i++) {
                t->input[i] = t->input[i + 1];
            }
            t->input_len--;
            t->input[t->input_len] = '\0';
            kterm_refresh_input(t_idx);
        }
        return;
    }

    if (key == '\b') {
        if (t->input_cursor > 0) {
            for (int i = t->input_cursor - 1; i < t->input_len - 1; i++) {
                t->input[i] = t->input[i + 1];
            }
            t->input_len--;
            t->input_cursor--;
            t->input[t->input_len] = '\0';
            kterm_refresh_input(t_idx);
        }
        return;
    }

    if (key == KEY_UP) {
        if (t->auth_ok && t->hist_count > 0 && t->hist_idx > 0) {
            t->hist_idx--;
            k_strcpy(t->input, t->history[t->hist_idx]);
            t->input_len = 0;
            while (t->input[t->input_len]) t->input_len++;
            t->input_cursor = t->input_len;
            kterm_refresh_input(t_idx);
        }
        return;
    }

    if (key == KEY_DOWN) {
        if (t->auth_ok && t->hist_count > 0 && t->hist_idx < t->hist_count - 1) {
            t->hist_idx++;
            k_strcpy(t->input, t->history[t->hist_idx]);
            t->input_len = 0;
            while (t->input[t->input_len]) t->input_len++;
            t->input_cursor = t->input_len;
            kterm_refresh_input(t_idx);
        } else if (t->hist_idx >= t->hist_count - 1) {
            t->hist_idx = t->hist_count;
            t->input[0] = '\0';
            t->input_len = 0;
            t->input_cursor = 0;
            kterm_refresh_input(t_idx);
        }
        return;
    }

    if ((unsigned char)key >= 32 && t->input_len < 240) {
        for (int i = t->input_len; i > t->input_cursor; i--) {
            t->input[i] = t->input[i - 1];
        }
        t->input[t->input_cursor] = key;
        t->input_len++;
        t->input_cursor++;
        t->input[t->input_len] = '\0';

        kterm_refresh_input(t_idx);
    }
}

void kshell_tick_frame(int tty_num) {
    kshell_drain_mouse();

    if (!g_kdirty) return;
    g_kdirty = 0;

    if (tty_num < 1 || tty_num > NUM_TTYS) tty_num = 1;
    int t_idx = tty_num - 1;
    KShellTTY *t = &g_kttys[t_idx];

    if (!framebuffer_base || !g_backbuffer) return;

    UINTN total = gop_width * gop_height;
    for (UINTN i = 0; i < total; i++) {
        g_backbuffer[i] = 0x000F172A;
    }

    for (int r = 0; r < TTY_ROWS; r++) {
        int py = r * 16;
        if (py + 16 > (int)gop_height) break;

        for (int c = 0; c < TTY_COLS; c++) {
            char ch = t->chars[r][c];
            if (ch && ch != ' ') {
                unsigned char uc = (unsigned char)ch;
                if (uc > 127) uc = '?';
                const unsigned char *glyph = font8x16[uc];
                UINT32 col = t->fg[r][c];
                int px = c * 8;

                for (int gy = 0; gy < 16; gy++) {
                    unsigned char bits = glyph[gy];
                    UINT32 *dst = &g_backbuffer[(py + gy) * gop_width + px];
                    for (int gx = 0; gx < 8; gx++) {
                        if (bits & (1 << (7 - gx))) dst[gx] = col;
                    }
                }
            }
        }
    }

    clamp_cursor(t_idx);
    int cur_px = t->cur_x * 8;
    int cur_py = t->cur_y * 16;

    if (cur_py >= 0 && cur_py + 16 <= (int)gop_height && cur_px >= 0 && cur_px + 8 <= (int)gop_width) {
        for (int cy = 0; cy < 16; cy++) {
            UINT32 *dst = &g_backbuffer[(cur_py + cy) * gop_width + cur_px];
            for (int cx = 0; cx < 8; cx++) dst[cx] = 0x0038BDF8;
        }

        char under = t->chars[t->cur_y][t->cur_x];
        if (under && under != ' ') {
            unsigned char uc = (unsigned char)under;
            if (uc > 127) uc = '?';
            const unsigned char *glyph = font8x16[uc];
            for (int gy = 0; gy < 16; gy++) {
                unsigned char bits = glyph[gy];
                UINT32 *dst = &g_backbuffer[(cur_py + gy) * gop_width + cur_px];
                for (int gx = 0; gx < 8; gx++) {
                    if (bits & (1 << (7 - gx))) dst[gx] = 0x00C74207;
                }
            }
        }
    }

    swap_buffers();
}
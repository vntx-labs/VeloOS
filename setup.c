#include "setup.h"
#include "font.h"
#include "wm.h"
#include "mouse.h"
#include "keyboard.h"
#include "desktop.h"
#include "ahci.h"
#include "fat32.h"
#include "sched.h"

extern UINTN gop_width;
extern UINTN gop_height;
void klog(const char *s);

void put_pixel(UINTN x, UINTN y, UINT32 color);
void clear_screen_graphics(UINT32 color);
void draw_filled_rect(UINTN start_x, UINTN start_y, UINTN width, UINTN height, UINT32 color);
void swap_buffers(void);

static int g_setup_active = 0;
static int g_step = 0;

char g_user_name_active[32] = "Benutzer";
char g_pc_name_active[32] = "Velo-PC";

static char g_username_input[32] = "Benutzer";
static char g_password_input[32] = "";
static char g_pcname_input[32] = "Velo-PC";
static int g_u_len = 8;
static int g_p_len = 0;
static int g_pc_len = 7;
static int g_focus = 0;

static int g_selected_lang = 0;
static int g_selected_tz = 0;
static int g_dst_auto = 1;
static int g_selected_avatar = 0;
static int g_progress = 0;

static UINT64 g_disk_mb = 64;
static char g_disk_label[48] = "Laufwerk 0 (AHCI SATA Disk)";

#define NUM_TZ 4
static const char *g_timezones[NUM_TZ] = {
    "(UTC+01:00) Amsterdam, Berlin, Bern, Rom, Wien",
    "(UTC+00:00) Dublin, Edinburgh, Lissabon, London",
    "(UTC-05:00) Eastern Time (USA und Kanada)",
    "(UTC+09:00) Osaka, Sapporo, Tokio"
};

static void detect_disk_info(void) {
    int port_count = ahci_get_port_count();
    for (int p = 0; p < port_count; p++) {
        AHCI_PORT_INFO *info = ahci_get_port_info(p);
        if (!info || !info->active) continue;

        UINT64 bytes = info->sector_count * 512;
        g_disk_mb = (bytes > 0) ? (bytes / (1024 * 1024)) : 64;

        char *ptr = g_disk_label;
        const char *name = info->model[0] ? info->model : "QEMU HARDDISK";
        while (*name && (ptr - g_disk_label < 40)) *ptr++ = *name++;
        *ptr = '\0';
        return;
    }
}

static void draw_velo_logo(int cx, int cy, int r) {
    draw_rounded_rect_aa(cx - r, cy - r, r * 2, r * 2, r, 0x005AC0E0);
    draw_rounded_rect_gradient(cx - r + 2, cy - r + 2, (r - 2) * 2, (r - 2) * 2, r - 2, 0x001B62D6, 0x000A2C68);

    int size = r / 2;
    draw_line_aa(cx - size, cy - size + 2, cx - size / 4, cy + size, 3, 0x00FFFFFF);
    draw_line_aa(cx - size / 4, cy + size, cx + size - 2, cy - size, 3, 0x00FFFFFF);
    draw_line_aa(cx - size - 2, cy + 1, cx + size + 4, cy - 2, 3, 0x005AC0E0);
    draw_line_aa(cx - size - 1, cy + 1, cx + size + 2, cy - 2, 1, 0x00FFFFFF);
}

static void render_velo_setup(void) {
    draw_rounded_rect_gradient(0, 0, (int)gop_width, (int)gop_height, 0, 0x00062838, 0x00021018);

    int win_w = 680;
    int win_h = 440;
    int win_x = ((int)gop_width - win_w) / 2;
    int win_y = ((int)gop_height - win_h) / 2;

    draw_rounded_rect_aa(win_x + 8, win_y + 8, win_w, win_h, 10, 0x00010810);
    draw_rounded_rect_aa(win_x, win_y, win_w, win_h, 8, 0x004A8FA8);
    draw_rounded_rect_aa(win_x + 1, win_y + 1, win_w - 2, win_h - 2, 7, 0x000B1A24);

    draw_rounded_rect_gradient(win_x + 2, win_y + 2, win_w - 4, 38, 6, 0x001B485A, 0x000B2430);
    draw_velo_logo(win_x + 24, win_y + 19, 14);
    wm_draw_text("Velo OS Installation", win_x + 48, win_y + 12, 0x00FFFFFF, 0x00000000);
    draw_filled_rect(win_x + 2, win_y + 40, win_w - 4, 1, 0x003A7088);

    if (g_step == 0) {
        wm_draw_text("Waehlen Sie die Sprache und Tastatureinstellungen aus:", win_x + 28, win_y + 60, 0x00FFFFFF, 0x00000000);
        wm_draw_text("Klicken Sie mit der Maus auf die gewuenschte Option:", win_x + 28, win_y + 82, 0x008AB8C8, 0x00000000);

        UINT32 opt1_border = (g_selected_lang == 0) ? 0x005AC0E0 : 0x002B5268;
        UINT32 opt1_bg = (g_selected_lang == 0) ? 0x001B485A : 0x000E222E;
        draw_rounded_rect_aa(win_x + 28, win_y + 115, win_w - 56, 52, 6, opt1_border);
        draw_rounded_rect_aa(win_x + 29, win_y + 116, win_w - 58, 50, 5, opt1_bg);
        wm_draw_text(g_selected_lang == 0 ? "(o) Deutsch (Deutschland)" : "( ) Deutsch (Deutschland)", win_x + 44, win_y + 128, 0x00FFFFFF, 0x00000000);
        wm_draw_text("Tastaturlayout: Deutsch (QWERTZ)", win_x + 72, win_y + 146, 0x008AB8C8, 0x00000000);

        UINT32 opt2_border = (g_selected_lang == 1) ? 0x005AC0E0 : 0x002B5268;
        UINT32 opt2_bg = (g_selected_lang == 1) ? 0x001B485A : 0x000E222E;
        draw_rounded_rect_aa(win_x + 28, win_y + 180, win_w - 56, 52, 6, opt2_border);
        draw_rounded_rect_aa(win_x + 29, win_y + 181, win_w - 58, 50, 5, opt2_bg);
        wm_draw_text(g_selected_lang == 1 ? "(o) English (United States)" : "( ) English (United States)", win_x + 44, win_y + 193, 0x00FFFFFF, 0x00000000);
        wm_draw_text("Keyboard Layout: US-International", win_x + 72, win_y + 211, 0x008AB8C8, 0x00000000);

        int btn_w = 120; int btn_h = 36;
        int btn_x = win_x + win_w - btn_w - 28;
        int btn_y = win_y + win_h - btn_h - 20;
        draw_rounded_rect_gradient(btn_x, btn_y, btn_w, btn_h, 6, 0x002F82A0, 0x00134A60);
        draw_rounded_rect_aa(btn_x, btn_y, btn_w, btn_h, 6, 0x005AC0E0);
        wm_draw_text("Weiter >", btn_x + 26, btn_y + 10, 0x00FFFFFF, 0x00000000);
    }
    else if (g_step == 1) {
        wm_draw_text("Ueberpruefen Sie die Datums- und Zeiteinstellungen:", win_x + 28, win_y + 55, 0x00FFFFFF, 0x00000000);
        wm_draw_text("Zeitzone auswaehlen:", win_x + 28, win_y + 80, 0x008AB8C8, 0x00000000);

        int tz_y = win_y + 105;
        for (int i = 0; i < NUM_TZ; i++) {
            UINT32 border = (g_selected_tz == i) ? 0x005AC0E0 : 0x002B5268;
            UINT32 bg = (g_selected_tz == i) ? 0x001B485A : 0x000E222E;
            draw_rounded_rect_aa(win_x + 28, tz_y, win_w - 56, 36, 6, border);
            draw_rounded_rect_aa(win_x + 29, tz_y + 1, win_w - 58, 34, 5, bg);
            wm_draw_text(g_selected_tz == i ? "[*]" : "[ ]", win_x + 40, tz_y + 10, 0x00FFFFFF, 0x00000000);
            wm_draw_text(g_timezones[i], win_x + 72, tz_y + 10, 0x00FFFFFF, 0x00000000);
            tz_y += 42;
        }

        draw_rounded_rect_aa(win_x + 28, tz_y + 10, 20, 20, 4, 0x005AC0E0);
        draw_rounded_rect_aa(win_x + 29, tz_y + 11, 18, 18, 3, g_dst_auto ? 0x002563EB : 0x0008141C);
        if (g_dst_auto) wm_draw_text("X", win_x + 34, tz_y + 12, 0x00FFFFFF, 0x00000000);
        wm_draw_text("Uhr automatisch an Sommer-/Winterzeit anpassen (MESZ/MEZ)", win_x + 58, tz_y + 12, 0x00FFFFFF, 0x00000000);

        int btn_w = 120; int btn_h = 36;
        int btn_x = win_x + win_w - btn_w - 28;
        int btn_y = win_y + win_h - btn_h - 20;

        draw_rounded_rect_gradient(btn_x - 130, btn_y, btn_w, btn_h, 6, 0x001B3644, 0x000E222E);
        draw_rounded_rect_aa(btn_x - 130, btn_y, btn_w, btn_h, 6, 0x002B5268);
        wm_draw_text("< Zurueck", btn_x - 130 + 22, btn_y + 10, 0x00FFFFFF, 0x00000000);

        draw_rounded_rect_gradient(btn_x, btn_y, btn_w, btn_h, 6, 0x002F82A0, 0x00134A60);
        draw_rounded_rect_aa(btn_x, btn_y, btn_w, btn_h, 6, 0x005AC0E0);
        wm_draw_text("Weiter >", btn_x + 26, btn_y + 10, 0x00FFFFFF, 0x00000000);
    }
    else if (g_step == 2) {
        wm_draw_text("Benutzerkonto und Computernamen anlegen:", win_x + 28, win_y + 55, 0x00FFFFFF, 0x00000000);

        wm_draw_text("Benutzerbild:", win_x + win_w - 180, win_y + 85, 0x008AB8C8, 0x00000000);
        int av_x = win_x + win_w - 180;
        int av_y = win_y + 105;
        for (int a = 0; a < 3; a++) {
            UINT32 av_col = (a == 0) ? 0x001B62D6 : ((a == 1) ? 0x00EA580C : 0x0016A34A);
            draw_rounded_rect_aa(av_x + a * 44, av_y, 36, 36, 18, (g_selected_avatar == a) ? 0x005AC0E0 : 0x002B5268);
            draw_rounded_rect_aa(av_x + a * 44 + 2, av_y + 2, 32, 32, 16, av_col);
        }

        wm_draw_text("Benutzername (z. B. Max):", win_x + 28, win_y + 85, 0x008AB8C8, 0x00000000);
        draw_rounded_rect_aa(win_x + 28, win_y + 105, 380, 36, 6, (g_focus == 0) ? 0x005AC0E0 : 0x002B5268);
        draw_rounded_rect_aa(win_x + 29, win_y + 106, 378, 34, 5, 0x0008141C);
        wm_draw_text(g_username_input, win_x + 40, win_y + 115, 0x00FFFFFF, 0x00000000);

        wm_draw_text("Kennwort (optional):", win_x + 28, win_y + 155, 0x008AB8C8, 0x00000000);
        draw_rounded_rect_aa(win_x + 28, win_y + 175, 380, 36, 6, (g_focus == 1) ? 0x005AC0E0 : 0x002B5268);
        draw_rounded_rect_aa(win_x + 29, win_y + 176, 378, 34, 5, 0x0008141C);
        char masked[32]; int p = 0; while(g_password_input[p]) { masked[p++] = '*'; } masked[p] = '\0';
        wm_draw_text(masked, win_x + 40, win_y + 185, 0x00FFFFFF, 0x00000000);

        wm_draw_text("Computername:", win_x + 28, win_y + 225, 0x008AB8C8, 0x00000000);
        draw_rounded_rect_aa(win_x + 28, win_y + 245, 380, 36, 6, (g_focus == 2) ? 0x005AC0E0 : 0x002B5268);
        draw_rounded_rect_aa(win_x + 29, win_y + 246, 378, 34, 5, 0x0008141C);
        wm_draw_text(g_pcname_input, win_x + 40, win_y + 255, 0x00FFFFFF, 0x00000000);

        int btn_w = 120; int btn_h = 36;
        int btn_x = win_x + win_w - btn_w - 28;
        int btn_y = win_y + win_h - btn_h - 20;

        draw_rounded_rect_gradient(btn_x - 130, btn_y, btn_w, btn_h, 6, 0x001B3644, 0x000E222E);
        draw_rounded_rect_aa(btn_x - 130, btn_y, btn_w, btn_h, 6, 0x002B5268);
        wm_draw_text("< Zurueck", btn_x - 130 + 22, btn_y + 10, 0x00FFFFFF, 0x00000000);

        draw_rounded_rect_gradient(btn_x, btn_y, btn_w, btn_h, 6, 0x002F82A0, 0x00134A60);
        draw_rounded_rect_aa(btn_x, btn_y, btn_w, btn_h, 6, 0x005AC0E0);
        wm_draw_text("Weiter >", btn_x + 26, btn_y + 10, 0x00FFFFFF, 0x00000000);
    }
    else if (g_step == 3) {
        wm_draw_text("Wo moechten Sie Velo installieren?", win_x + 28, win_y + 60, 0x00FFFFFF, 0x00000000);
        wm_draw_text("Waehlen Sie die Ziel-Festplatte fuer das System aus:", win_x + 28, win_y + 85, 0x008AB8C8, 0x00000000);

        draw_rounded_rect_aa(win_x + 28, win_y + 115, win_w - 56, 56, 6, 0x005AC0E0);
        draw_rounded_rect_aa(win_x + 29, win_y + 116, win_w - 58, 54, 5, 0x001B485A);

        char drive_str[80];
        int pos = 0;
        const char *t = "[*] Datentraeger 0: "; while(*t) drive_str[pos++] = *t++;
        const char *m = g_disk_label; while(*m && pos < 50) drive_str[pos++] = *m++;
        drive_str[pos++] = ' '; drive_str[pos++] = '(';
        if (g_disk_mb >= 1000) {
            drive_str[pos++] = '0' + (char)(g_disk_mb / 1000);
            drive_str[pos++] = 'G'; drive_str[pos++] = 'B';
        } else {
            if (g_disk_mb >= 100) drive_str[pos++] = '0' + (char)(g_disk_mb / 100);
            if (g_disk_mb >= 10) drive_str[pos++] = '0' + (char)((g_disk_mb / 10) % 10);
            drive_str[pos++] = '0' + (char)(g_disk_mb % 10);
            drive_str[pos++] = 'M'; drive_str[pos++] = 'B';
        }
        drive_str[pos++] = ')'; drive_str[pos] = '\0';
        wm_draw_text(drive_str, win_x + 44, win_y + 128, 0x00FFFFFF, 0x00000000);
        wm_draw_text("Status: Bereit zur Formatierung & Strukturierung", win_x + 72, win_y + 148, 0x008AB8C8, 0x00000000);

        int btn_w = 130; int btn_h = 36;
        int btn_x = win_x + win_w - btn_w - 28;
        int btn_y = win_y + win_h - btn_h - 20;

        draw_rounded_rect_gradient(btn_x - 140, btn_y, 130, btn_h, 6, 0x001B3644, 0x000E222E);
        draw_rounded_rect_aa(btn_x - 140, btn_y, 130, btn_h, 6, 0x002B5268);
        wm_draw_text("< Zurueck", btn_x - 140 + 26, btn_y + 10, 0x00FFFFFF, 0x00000000);

        draw_rounded_rect_gradient(btn_x, btn_y, btn_w, btn_h, 6, 0x002F82A0, 0x00134A60);
        draw_rounded_rect_aa(btn_x, btn_y, btn_w, btn_h, 6, 0x005AC0E0);
        wm_draw_text("Installieren", btn_x + 20, btn_y + 10, 0x00FFFFFF, 0x00000000);
    }
    else if (g_step == 4) {
        wm_draw_text("Velo-Dateisystem und Ordnerstruktur werden erstellt...", win_x + 28, win_y + 65, 0x00FFFFFF, 0x00000000);

        wm_draw_text(g_progress >= 25 ? "[+] FAT32 Partition & Bootsektoren (100%)" : "[~] FAT32 formatieren...", win_x + 36, win_y + 115, g_progress >= 25 ? 0x004ADE80 : 0x005AC0E0, 0x00000000);
        wm_draw_text(g_progress >= 55 ? "[+] VeloOS & System32 Verzeichnisse (100%)" : "[~] VeloOS-Ordnerstrukturen anlegen...", win_x + 36, win_y + 150, g_progress >= 55 ? 0x004ADE80 : (g_progress >= 25 ? 0x005AC0E0 : 0x004A6878), 0x00000000);
        wm_draw_text(g_progress >= 85 ? "[+] Benutzerprofile (Desktop, Dokumente) (100%)" : "[~] Benutzerprofile einrichten...", win_x + 36, win_y + 185, g_progress >= 85 ? 0x004ADE80 : (g_progress >= 55 ? 0x005AC0E0 : 0x004A6878), 0x00000000);
        wm_draw_text(g_progress == 100 ? "[+] Systemkonfiguration speichern (100%)" : "[ ] Konfiguration speichern...", win_x + 36, win_y + 220, g_progress == 100 ? 0x004ADE80 : 0x004A6878, 0x00000000);

        int bar_x = win_x + 28;
        int bar_y = win_y + 280;
        int bar_w = win_w - 56;
        int bar_h = 20;

        draw_rounded_rect_aa(bar_x, bar_y, bar_w, bar_h, 6, 0x002B5268);
        draw_rounded_rect_aa(bar_x + 1, bar_y + 1, bar_w - 2, bar_h - 2, 5, 0x0008141C);

        int fill_w = ((bar_w - 4) * g_progress) / 100;
        if (fill_w > 0) {
            draw_rounded_rect_gradient(bar_x + 2, bar_y + 2, fill_w, bar_h - 4, 4, 0x0040D050, 0x00188028);
        }

        char p_str[16];
        p_str[0] = '0' + (char)(g_progress / 100);
        p_str[1] = '0' + (char)((g_progress / 10) % 10);
        p_str[2] = '0' + (char)(g_progress % 10);
        p_str[3] = '%'; p_str[4] = '\0';
        wm_draw_text(p_str, win_x + win_w - 70, win_y + 310, 0x005AC0E0, 0x00000000);
    }
    else if (g_step == 5) {
        clear_screen_graphics(0x00021018);
        draw_velo_logo((int)gop_width / 2, (int)gop_height / 2 - 40, 24);
        wm_draw_text("Velo Desktop wird vorbereitet...", (int)gop_width / 2 - 140, (int)gop_height / 2 + 10, 0x00FFFFFF, 0x00000000);
    }

    mouse_draw_cursor();
    swap_buffers();
}

static void create_windows_folder_structure_on_disk(void *port) {
    fat32_mkdir(port, "/VeloOS");
    fat32_mkdir(port, "/VeloOS/System32");
    fat32_mkdir(port, "/Program Files");
    fat32_mkdir(port, "/Users");
    fat32_mkdir(port, "/Users/Desktop");
    fat32_mkdir(port, "/Users/Documents");
    fat32_mkdir(port, "/Users/Downloads");
    fat32_mkdir(port, "/Users/Pictures");
    fat32_mkdir(port, "/Users/Music");
    fat32_mkdir(port, "/Users/Videos");
    fat32_mkdir(port, "/Users/Public");

    const char *welcome_txt = "Willkommen bei VeloOS!\nDies ist ein echtes FAT32 Dateisystem mit VeloOS-Ordnerstruktur.";
    fat32_write_file(port, "README.TXT", (void*)welcome_txt, 86);
}

static void save_system_config_to_disk(void) {
    SystemConfig cfg;
    __builtin_memset(&cfg, 0, sizeof(SystemConfig));

    __builtin_memcpy(cfg.magic, "VELO_CFG", 8);
    __builtin_memcpy(cfg.username, g_username_input, 32);
    __builtin_memcpy(cfg.password, g_password_input, 32);
    __builtin_memcpy(cfg.pcname, g_pcname_input, 32);
    cfg.timezone_idx = g_selected_tz;
    cfg.dst_auto = g_dst_auto;
    cfg.lang = g_selected_lang;
    cfg.avatar = g_selected_avatar;
    cfg.setup_completed = 1;

    int port_count = ahci_get_port_count();
    for (int p = 0; p < port_count; p++) {
        AHCI_PORT_INFO *info = ahci_get_port_info(p);
        if (!info || !info->active) continue;

        create_windows_folder_structure_on_disk(info->port_addr);
        fat32_write_file(info->port_addr, "CONFIG.DAT", &cfg, sizeof(SystemConfig));
        return;
    }
}

int load_system_config(SystemConfig *out_cfg) {
    if (!out_cfg) return 0;
    int port_count = ahci_get_port_count();
    for (int p = 0; p < port_count; p++) {
        AHCI_PORT_INFO *info = ahci_get_port_info(p);
        if (!info || !info->active) continue;

        __builtin_memset(out_cfg, 0, sizeof(SystemConfig));
        int bytes = fat32_read_file(info->port_addr, "CONFIG.DAT", out_cfg, sizeof(SystemConfig));
        if (bytes > 0 && out_cfg->magic[0] == 'V' && out_cfg->magic[1] == 'E' && out_cfg->setup_completed == 1) {
            return 1;
        }
    }
    return 0;
}

static void run_setup_installation(void) {
    g_step = 4;

    g_progress = 15;
    render_velo_setup();
    task_sleep(2);

    g_progress = 40;
    render_velo_setup();
    task_sleep(2);

    if (g_u_len > 0) {
        int i = 0;
        while (g_username_input[i] && i < 31) {
            g_user_name_active[i] = g_username_input[i];
            i++;
        }
        g_user_name_active[i] = '\0';
    }

    g_progress = 70;
    render_velo_setup();
    save_system_config_to_disk();

    g_progress = 100;
    render_velo_setup();
    task_sleep(3);

    g_step = 5;
    render_velo_setup();
    task_sleep(4);

    g_setup_active = 0;
    desktop_start();
}

void setup_init(void) {
    klog("[+] setup_init: Starte Hardware-Erkennung...\n");
    g_setup_active = 1;
    g_step = 0;
    g_focus = 0;
    g_progress = 0;

    detect_disk_info();
    klog("[+] setup_init: Initialisiere Maus...\n");
    mouse_init();
    keyboard_set_layout(g_selected_lang);
    klog("[+] setup_init: Rendere Setup-Oberflaeche...\n");
    render_velo_setup();
    klog("[+] setup_init: Fertig! UI aktiv.\n");
}

void setup_disable(void) {
    g_setup_active = 0;
}

void setup_tick(void) {
    if (!g_setup_active) return;

    mouse_update();
    MouseState *m = mouse_get_state();

    int win_w = 680;
    int win_h = 440;
    int win_x = ((int)gop_width - win_w) / 2;
    int win_y = ((int)gop_height - win_h) / 2;

    int cursor = CURSOR_ARROW;

    if (g_step == 0) {
        if ((m->x >= win_x + 28 && m->x <= win_x + win_w - 28 && m->y >= win_y + 115 && m->y <= win_y + 232) ||
            (m->x >= win_x + win_w - 148 && m->x <= win_x + win_w - 28 && m->y >= win_y + win_h - 56 && m->y <= win_y + win_h - 20)) {
            cursor = CURSOR_HAND;
        }
    } else if (g_step == 1) {
        if ((m->x >= win_x + 28 && m->x <= win_x + win_w - 28 && m->y >= win_y + 105 && m->y <= win_y + 270) ||
            (m->x >= win_x + win_w - 278 && m->x <= win_x + win_w - 28 && m->y >= win_y + win_h - 56 && m->y <= win_y + win_h - 20)) {
            cursor = CURSOR_HAND;
        }
    } else if (g_step == 2) {
        if ((m->x >= win_x + 28 && m->x <= win_x + 408 && m->y >= win_y + 105 && m->y <= win_y + 141) ||
            (m->x >= win_x + 28 && m->x <= win_x + 408 && m->y >= win_y + 175 && m->y <= win_y + 211) ||
            (m->x >= win_x + 28 && m->x <= win_x + 408 && m->y >= win_y + 245 && m->y <= win_y + 281)) {
            cursor = CURSOR_IBEAM;
        } else if ((m->x >= win_x + win_w - 180 && m->x <= win_x + win_w - 50 && m->y >= win_y + 105 && m->y <= win_y + 141) ||
                 (m->x >= win_x + win_w - 278 && m->x <= win_x + win_w - 28 && m->y >= win_y + win_h - 56 && m->y <= win_y + win_h - 20)) {
            cursor = CURSOR_HAND;
        }
    } else if (g_step == 3) {
        if (m->x >= win_x + win_w - 298 && m->x <= win_x + win_w - 28 && m->y >= win_y + win_h - 56 && m->y <= win_y + win_h - 20) {
            cursor = CURSOR_HAND;
        }
    }

    mouse_set_cursor(cursor);

    if (m->left_clicked) {
        m->left_clicked = 0;
        if (g_step == 0) {
            if (m->x >= win_x + 28 && m->x <= win_x + win_w - 28 && m->y >= win_y + 115 && m->y <= win_y + 167) {
                g_selected_lang = 0;
                keyboard_set_layout(LAYOUT_QWERTZ);
            }
            else if (m->x >= win_x + 28 && m->x <= win_x + win_w - 28 && m->y >= win_y + 180 && m->y <= win_y + 232) {
                g_selected_lang = 1;
                keyboard_set_layout(LAYOUT_QWERTY);
            }
            else if (m->x >= win_x + win_w - 148 && m->x <= win_x + win_w - 28 && m->y >= win_y + win_h - 56 && m->y <= win_y + win_h - 20) g_step = 1;
        }
        else if (g_step == 1) {
            int tz_y = win_y + 105;
            for (int i = 0; i < NUM_TZ; i++) {
                if (m->x >= win_x + 28 && m->x <= win_x + win_w - 28 && m->y >= tz_y && m->y <= tz_y + 36) {
                    g_selected_tz = i;
                }
                tz_y += 42;
            }
            if (m->x >= win_x + 28 && m->x <= win_x + win_w - 28 && m->y >= tz_y && m->y <= tz_y + 30) {
                g_dst_auto = !g_dst_auto;
            }

            int btn_x = win_x + win_w - 148;
            int btn_y = win_y + win_h - 56;
            if (m->x >= btn_x - 130 && m->x <= btn_x - 10 && m->y >= btn_y && m->y <= btn_y + 36) g_step = 0;
            else if (m->x >= btn_x && m->x <= btn_x + 120 && m->y >= btn_y && m->y <= btn_y + 36) g_step = 2;
        }
        else if (g_step == 2) {
            if (m->x >= win_x + 28 && m->x <= win_x + 408 && m->y >= win_y + 105 && m->y <= win_y + 141) g_focus = 0;
            else if (m->x >= win_x + 28 && m->x <= win_x + 408 && m->y >= win_y + 175 && m->y <= win_y + 211) g_focus = 1;
            else if (m->x >= win_x + 28 && m->x <= win_x + 408 && m->y >= win_y + 245 && m->y <= win_y + 281) g_focus = 2;

            int av_x = win_x + win_w - 180;
            int av_y = win_y + 105;
            for (int a = 0; a < 3; a++) {
                if (m->x >= av_x + a * 44 && m->x <= av_x + a * 44 + 36 && m->y >= av_y && m->y <= av_y + 36) g_selected_avatar = a;
            }

            int btn_x = win_x + win_w - 148;
            int btn_y = win_y + win_h - 56;
            if (m->x >= btn_x - 130 && m->x <= btn_x - 10 && m->y >= btn_y && m->y <= btn_y + 36) g_step = 1;
            else if (m->x >= btn_x && m->x <= btn_x + 120 && m->y >= btn_y && m->y <= btn_y + 36) g_step = 3;
        }
        else if (g_step == 3) {
            int btn_x = win_x + win_w - 158;
            int btn_y = win_y + win_h - 56;
            if (m->x >= btn_x - 140 && m->x <= btn_x - 10 && m->y >= btn_y && m->y <= btn_y + 36) g_step = 2;
            else if (m->x >= btn_x && m->x <= btn_x + 130 && m->y >= btn_y && m->y <= btn_y + 36) {
                run_setup_installation();
                return;
            }
        }
    }
    render_velo_setup();
}

void setup_handle_key(char c) {
    if (!g_setup_active) return;
    char tc = keyboard_translate_char(c);

    if (g_step == 0) {
        if (tc == '\n' || tc == '\r') { g_step = 1; render_velo_setup(); }
    }
    else if (g_step == 1) {
        if (tc == '\n' || tc == '\r') { g_step = 2; render_velo_setup(); }
    }
    else if (g_step == 2) {
        if (tc == '\t') {
            g_focus = (g_focus + 1) % 3;
            render_velo_setup();
        } else if (tc == '\n' || tc == '\r') {
            if (g_focus < 2) { g_focus++; render_velo_setup(); }
            else { g_step = 3; render_velo_setup(); }
        } else if (tc == '\b') {
            if (g_focus == 0 && g_u_len > 0) g_username_input[--g_u_len] = '\0';
            else if (g_focus == 1 && g_p_len > 0) g_password_input[--g_p_len] = '\0';
            else if (g_focus == 2 && g_pc_len > 0) g_pcname_input[--g_pc_len] = '\0';
            render_velo_setup();
        } else if (tc >= 32 && tc <= 126) {
            if (g_focus == 0 && g_u_len < 30) { g_username_input[g_u_len++] = tc; g_username_input[g_u_len] = '\0'; }
            else if (g_focus == 1 && g_p_len < 30) { g_password_input[g_p_len++] = tc; g_password_input[g_p_len] = '\0'; }
            else if (g_focus == 2 && g_pc_len < 30) { g_pcname_input[g_pc_len++] = tc; g_pcname_input[g_pc_len] = '\0'; }
            render_velo_setup();
        }
    }
    else if (g_step == 3) {
        if (tc == '\n' || tc == '\r') run_setup_installation();
    }
}

int setup_is_active(void) {
    return g_setup_active;
}
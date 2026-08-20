// kernel.c - UEFI 64-Disk-Persistent OS Kernel (Strict No-RAM Account Storage)
#include <efi.h>
#include <efilib.h>
#include "keyboard.h"
#include "font.h"
#include "ahci.h"
#include "fat32.h"

// Shell-Zustandstracker & Globale UEFI-Referenzen
char command_buffer[64];
int command_length = 0;
EFI_SYSTEM_TABLE* g_st;
EFI_HANDLE g_image_handle;

// AHCI / Festplatten-Zustand
UINTN g_ahci_abar = 0;
static void* active_ahci_port = NULL;

// GOP Grafik-Variablen & Cursor
EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
UINTN gop_width = 0;
UINTN gop_height = 0;
VOID* framebuffer_base = NULL;
UINTN framebuffer_size = 0;

UINTN gfx_cursor_x = 50;
UINTN gfx_cursor_y = 190;

int is_bare_metal = 0;     // Flag für UEFI Exit

// System-Modi: 0 = Shell, 1 = Login-Screen, 2 = Account Setup Wizard, 3 = Desktop
int system_mode = 0; 

// Login State (Modus 1)
char login_username[32];
char login_password[32];
int login_field_focus = 0; // 0: User, 1: Pass, 2: Login-Button
int login_len_u = 0;
int login_len_p = 0;
int login_error = 0;

// Account Creator State (Modus 2)
char ui_username[32];
char ui_password[32];
int ui_field_focus = 0; // 0: Username, 1: Password, 2: Speichern Button
int ui_input_len_u = 0;
int ui_input_len_p = 0;

// Desktop State (Modus 3)
int desktop_menu_open = 0;  
int desktop_menu_focus = 0; 

typedef struct {
    UINTN MapSize;
    EFI_MEMORY_DESCRIPTOR *Map;
    UINTN MapKey;
    UINTN DescriptorSize;
    UINT32 DescriptorVersion;
} MemoryMap;

MemoryMap mmap;

// Vorwärtsdeklarationen
void put_pixel(UINTN x, UINTN y, UINT32 color);
void clear_screen_graphics(UINT32 color);
void draw_string(const char* str, UINTN x, UINTN y, UINT32 fg_color, UINT32 bg_color);
void draw_filled_rect(UINTN start_x, UINTN start_y, UINTN width, UINTN height, UINT32 color);
void draw_window(UINTN x, UINTN y, UINTN w, UINTN h, const char* title);
void show_login_screen();
void show_desktop();
void show_account_creator_ui();
int check_account_exists();

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

// XOR-Verschlüsselung für Account-Sicherheit
void encrypt_data(char *data, UINT32 size) {
    char key = 0x5A;
    for (UINT32 i = 0; i < size; i++) {
        data[i] ^= key;
    }
}

// Prüft ausschließlich auf der Festplatte, ob ein Account existiert
int check_account_exists() {
    if (!active_ahci_port) return 0;
    char record[64];
    __builtin_memset(record, 0, 64);
    if (fat32_read_file(active_ahci_port, "ACCOUNT.DAT", record, 64)) {
        return 1;
    }
    return 0;
}

// Account speichern (Schreibt direkt auf Disk, KEIN dauerhaftes Speichern im RAM)
int save_account_to_disk(const char *user, const char *pass) {
    if (!active_ahci_port) return 0;

    char record[64];
    __builtin_memset(record, 0, 64);
    
    int i = 0;
    while (user[i] && i < 31) { record[i] = user[i]; i++; }
    i = 32;
    int j = 0;
    while (pass[j] && j < 31) { record[i++] = pass[j++]; }

    encrypt_data(record, 64);
    
    // Direkt auf Disk schreiben
    int result = fat32_write_file(active_ahci_port, "ACCOUNT.DAT", record, 64);
    
    // Sensible lokalen Daten im Stack/Puffer sofort löschen (Sicherheit)
    __builtin_memset(record, 0, 64);
    return result;
}

// Login verifizieren direkt von der Disk
int verify_login(const char *user, const char *pass) {
    if (!active_ahci_port) return 0;

    char record[64];
    __builtin_memset(record, 0, 64);

    if (!fat32_read_file(active_ahci_port, "ACCOUNT.DAT", record, 64)) {
        return 0; // Kein Account auf Disk gefunden
    }

    encrypt_data(record, 64); // Entschlüsseln

    char disk_user[32];
    char disk_pass[32];
    __builtin_memset(disk_user, 0, 32);
    __builtin_memset(disk_pass, 0, 32);

    for (int i = 0; i < 32; i++) disk_user[i] = record[i];
    for (int i = 0; i < 32; i++) disk_pass[i] = record[32 + i];

    int match = (strcmp(disk_user, user) == 0 && strcmp(disk_pass, pass) == 0);

    // Sensible Daten aus dem Stack löschen
    __builtin_memset(record, 0, 64);
    __builtin_memset(disk_pass, 0, 32);

    return match;
}

// Direkte Port I/O für x86 PCI & Shutdown
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

void system_shutdown() {
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    if (g_st && g_st->RuntimeServices) {
        g_st->RuntimeServices->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
    }
    while(1) { __asm__ volatile("cli; hlt"); }
}

void system_reboot() {
    if (g_st && g_st->RuntimeServices) {
        g_st->RuntimeServices->ResetSystem(EfiResetWarm, EFI_SUCCESS, 0, NULL);
    }
    outl(0xCF8, 0x8000F838);
    while(1) { __asm__ volatile("cli; hlt"); }
}

UINT32 pci_config_read(UINT8 bus, UINT8 slot, UINT8 func, UINT8 offset) {
    UINT32 address = (1U << 31) | ((UINT32)bus << 16) | ((UINT32)(slot & 0x1F) << 11) | ((UINT32)(func & 0x07) << 8) | (UINT32)(offset & 0xFC);
    outl(0xCF8, address);
    return inl(0xCFC);
}

void discover_ahci_bar_direct() {
    for (UINT8 slot = 0; slot < 32; slot++) {
        UINT32 vendor_device = pci_config_read(0, slot, 0, 0x00);
        if ((vendor_device & 0xFFFF) == 0xFFFF) continue;
        UINT32 class_code = pci_config_read(0, slot, 0, 0x08);
        if (((class_code >> 24) & 0xFF) == 0x01 && ((class_code >> 16) & 0xFF) == 0x06) {
            UINT32 bar5 = pci_config_read(0, slot, 0, 0x24);
            g_ahci_abar = (UINTN)(bar5 & 0xFFFFFFF0);
            break;
        }
    }
}

void init_gop() {
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_STATUS status = g_st->BootServices->LocateProtocol(&gop_guid, NULL, (VOID**)&gop);
    if (status != EFI_SUCCESS || gop == NULL) return;
    gop_width = gop->Mode->Info->HorizontalResolution;
    gop_height = gop->Mode->Info->VerticalResolution;
    framebuffer_base = (VOID*)gop->Mode->FrameBufferBase;
    framebuffer_size = gop->Mode->FrameBufferSize;
}

void put_pixel(UINTN x, UINTN y, UINT32 color) {
    if (framebuffer_base == NULL || x >= gop_width || y >= gop_height) return;
    UINT32* pixel_addr = (UINT32*)((UINT8*)framebuffer_base + (y * gop->Mode->Info->PixelsPerScanLine + x) * sizeof(UINT32));
    *pixel_addr = color;
}

void clear_screen_graphics(UINT32 color) {
    if (framebuffer_base == NULL) return;
    for (UINTN y = 0; y < gop_height; y++) {
        for (UINTN x = 0; x < gop_width; x++) put_pixel(x, y, color);
    }
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
    draw_filled_rect(x + 6, y + 6, w, h, 0x0008080C); 
    draw_filled_rect(x, y, w, h, 0x001E1E2E);         
    draw_filled_rect(x, y, w, 34, 0x00007ACC);        
    draw_string(title, x + 12, y + 9, 0x00FFFFFF, 0x00007ACC);
    draw_filled_rect(x + w - 30, y + 4, 26, 26, 0x00D32F2F);
    draw_string("X", x + w - 21, y + 9, 0x00FFFFFF, 0x00D32F2F);
}

// --- LOGIN-SCREEN (Modus 1) ---
void show_login_screen() {
    clear_screen_graphics(0x000F0F17);
    UINTN win_w = 480;
    UINTN win_h = 320;
    UINTN win_x = (gop_width - win_w) / 2;
    UINTN win_y = (gop_height - win_h) / 2;

    draw_window(win_x, win_y, win_w, win_h, "VeloOS - Secure Login");

    UINT32 col_u = (login_field_focus == 0) ? 0x0000FFCC : 0x00555566;
    draw_string("Benutzername:", win_x + 20, win_y + 55, 0x00FFFFFF, 0x001E1E2E);
    draw_filled_rect(win_x + 20, win_y + 80, 440, 35, 0x002A2A3C);
    draw_string(login_username, win_x + 30, win_y + 88, col_u, 0x002A2A3C);

    UINT32 col_p = (login_field_focus == 1) ? 0x0000FFCC : 0x00555566;
    draw_string("Passwort:", win_x + 20, win_y + 135, 0x00FFFFFF, 0x001E1E2E);
    draw_filled_rect(win_x + 20, win_y + 160, 440, 35, 0x002A2A3C);
    char masked[32]; int p = 0; while(login_password[p]) { masked[p++] = '*'; } masked[p] = '\0';
    draw_string(masked, win_x + 30, win_y + 168, col_p, 0x002A2A3C);

    UINT32 btn_bg = (login_field_focus == 2) ? 0x0000FF00 : 0x00007ACC;
    draw_filled_rect(win_x + 140, win_y + 225, 200, 40, btn_bg);
    draw_string("Anmelden", win_x + 180, win_y + 236, 0x00000000, btn_bg);

    draw_string("[TAB] Wechseln | [ENTER] OK | [ESC] Shell", win_x + 30, win_y + 285, 0x00777788, 0x001E1E2E);

    if (login_error) {
        draw_string("Falscher User oder Pass!", win_x + 25, win_y + 10, 0x00FF0000, 0x00007ACC);
    }
}

// --- ACCOUNT SETUP WIZARD (Modus 2) ---
void show_account_creator_ui() {
    clear_screen_graphics(0x000F0F17);
    UINTN win_w = 480;
    UINTN win_h = 320;
    UINTN win_x = (gop_width - win_w) / 2;
    UINTN win_y = (gop_height - win_h) / 2;

    draw_window(win_x, win_y, win_w, win_h, "VeloOS - Account Setup Wizard");
    
    draw_string("Ersten Administrator anlegen:", win_x + 20, win_y + 50, 0x0000FFCC, 0x001E1E2E);
    
    draw_string("Username:", win_x + 20, win_y + 90, 0x00FFFFFF, 0x001E1E2E);
    draw_filled_rect(win_x + 20, win_y + 115, 440, 30, 0x002A2A3C);
    draw_string(ui_username, win_x + 30, win_y + 121, 0x00FFFFFF, 0x002A2A3C);

    draw_string("Password:", win_x + 20, win_y + 160, 0x00FFFFFF, 0x001E1E2E);
    draw_filled_rect(win_x + 20, win_y + 185, 440, 30, 0x002A2A3C);
    char masked[32]; int p = 0; while(ui_password[p]) { masked[p++] = '*'; } masked[p] = '\0';
    draw_string(masked, win_x + 30, win_y + 191, 0x00FFFFFF, 0x002A2A3C);

    UINT32 btn_bg = (ui_field_focus == 2) ? 0x0000FF00 : 0x00007ACC;
    draw_filled_rect(win_x + 140, win_y + 235, 200, 40, btn_bg);
    draw_string("Speichern & Disk", win_x + 160, win_y + 245, 0x00000000, btn_bg);
    draw_string("[ESC] Abbrechen zur Shell", win_x + 120, win_y + 285, 0x00777788, 0x001E1E2E);
}

// --- DESKTOP (Modus 3) ---
void show_desktop() {
    clear_screen_graphics(0x0011111B);

    UINTN taskbar_h = 45;
    draw_filled_rect(0, gop_height - taskbar_h, gop_width, taskbar_h, 0x00181825);
    
    UINT32 start_bg = desktop_menu_open ? 0x0000FFCC : 0x00007ACC;
    draw_filled_rect(10, gop_height - 38, 110, 31, start_bg);
    draw_string("[F1] Start", 25, gop_height - 31, 0x00000000, start_bg);

    draw_string("Shortcuts: [F1] Startmenue | [ESC] Logout zur Shell", gop_width - 450, gop_height - 31, 0x00AAAAAA, 0x00181825);

    UINTN win_w = 600;
    UINTN win_h = 340;
    UINTN win_x = (gop_width - win_w) / 2;
    UINTN win_y = (gop_height - win_h) / 2 - 20;

    draw_window(win_x, win_y, win_w, win_h, "VeloOS Control Center - System Dashboard");
    draw_string("Status: Online, authentifiziert & FAT32 Storage aktiv.", win_x + 20, win_y + 55, 0x0000FFCC, 0x001E1E2E);
    draw_string("Benutzer: Erfolgreich eingeloggt (Disk-Persistent)", win_x + 20, win_y + 90, 0x00FFFFFF, 0x001E1E2E);

    if (desktop_menu_open) {
        UINTN menu_w = 200;
        UINTN menu_h = 140;
        UINTN menu_x = 10;
        UINTN menu_y = gop_height - taskbar_h - menu_h - 5;

        draw_filled_rect(menu_x + 4, menu_y + 4, menu_w, menu_h, 0x00050508);
        draw_filled_rect(menu_x, menu_y, menu_w, menu_h, 0x00222233);
        
        UINT32 c1 = (desktop_menu_focus == 1) ? 0x00007ACC : 0x00222233;
        draw_filled_rect(menu_x + 5, menu_y + 52, menu_w - 10, 32, c1);
        draw_string("  Logout", menu_x + 10, menu_y + 60, 0x00FFFFFF, c1);
    }
}

void perform_exit_boot_services() {
    if (is_bare_metal) return;

    mmap.MapSize = 0;
    g_st->BootServices->GetMemoryMap(&mmap.MapSize, NULL, &mmap.MapKey, &mmap.DescriptorSize, &mmap.DescriptorVersion);
    mmap.MapSize += 1024;
    VOID* temp_map_buffer = NULL;
    g_st->BootServices->AllocatePool(EfiLoaderData, mmap.MapSize, &temp_map_buffer);
    mmap.Map = (EFI_MEMORY_DESCRIPTOR*)temp_map_buffer;

    if (g_st->BootServices->GetMemoryMap(&mmap.MapSize, mmap.Map, &mmap.MapKey, &mmap.DescriptorSize, &mmap.DescriptorVersion) != EFI_SUCCESS) return;
    if (g_st->BootServices->ExitBootServices(g_image_handle, mmap.MapKey) != EFI_SUCCESS) return;

    is_bare_metal = 1;

    if (g_ahci_abar != 0) {
        init_ahci(g_ahci_abar);
        typedef volatile struct { UINT32 r[16]; } HBA_PORT;
        typedef volatile struct { UINT32 cap; UINT32 ghc; UINT32 is; UINT32 pi; UINT32 vs; UINT8 rsv[116]; UINT8 v[96]; HBA_PORT ports[32]; } HBA_MEM;
        HBA_MEM *hba = (HBA_MEM*)g_ahci_abar;
        for (int i = 0; i < 32; i++) {
            if (hba->pi & (1 << i)) { active_ahci_port = (void*)&hba->ports[i]; break; }
        }
        if (active_ahci_port) fat32_init(active_ahci_port);
    }

    system_mode = 0;
    clear_screen_graphics(0x00000000);
    draw_string("VeloOS Kernel - Bare Metal Mode Active", 50, 30, 0x00FFFFFF, 0x00000000);
    draw_string("Tippe 'help' fuer eine Liste aller Befehle.", 50, 60, 0x0000FFCC, 0x00000000);
    gfx_cursor_x = 50; gfx_cursor_y = 120;
    draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
    gfx_cursor_x += 32;
}

void execute_command() {
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
        return;
    }
    else if (strcmp(command_buffer, "login") == 0) {
        if (!is_bare_metal) {
            draw_string("Erst mit 'exit' in den Bare-Metal-Modus wechseln!", gfx_cursor_x, gfx_cursor_y, 0x00FF0000, 0x00000000);
        } else if (!check_account_exists()) {
            draw_string("Fehler: Kein Account vorhanden! Nutze 'account -create -ui'", gfx_cursor_x, gfx_cursor_y, 0x00FF0000, 0x00000000);
        } else {
            system_mode = 1;
            login_field_focus = 0;
            login_error = 0;
            __builtin_memset(login_username, 0, 32);
            __builtin_memset(login_password, 0, 32);
            login_len_u = 0; login_len_p = 0;
            show_login_screen();
            command_length = 0;
            return;
        }
    }
    else if (strcmp(command_buffer, "desktop") == 0) {
        if (!is_bare_metal) {
            draw_string("Erst mit 'exit' in den Bare-Metal-Modus wechseln!", gfx_cursor_x, gfx_cursor_y, 0x00FF0000, 0x00000000);
        } else {
            // SICHERHEITSPRÜFUNG: Ohne Account ODER Login kein Desktop!
            if (!check_account_exists()) {
                draw_string("Fehler: Kein Account vorhanden! Nutze 'account -create -ui'", gfx_cursor_x, gfx_cursor_y, 0x00FF0000, 0x00000000);
            } else {
                // Erzwinge Login-Screen vor dem Desktop, wenn nicht eingeloggt
                system_mode = 1;
                login_field_focus = 0;
                login_error = 0;
                __builtin_memset(login_username, 0, 32);
                __builtin_memset(login_password, 0, 32);
                login_len_u = 0; login_len_p = 0;
                show_login_screen();
                command_length = 0;
                return;
            }
        }
    }
    else if (strcmp(command_buffer, "account -create -ui") == 0) {
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
        system_shutdown();
    }
    else if (strcmp(command_buffer, "reboot") == 0) {
        draw_string("Starte System neu...", gfx_cursor_x, gfx_cursor_y, 0x00FFAA00, 0x00000000);
        system_reboot();
    }
    else if (strcmp(command_buffer, "exit") == 0) {
        if (is_bare_metal) {
            draw_string("Bereits im Bare-Metal-Modus!", gfx_cursor_x, gfx_cursor_y, 0x00FF0000, 0x00000000);
        } else {
            draw_string("Wechsle in den Bare-Metal-Modus...", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
            perform_exit_boot_services();
            command_length = 0;
            return;
        }
    }
    else if (strcmp(command_buffer, "help") == 0) {
        draw_string("Befehle: exit, login, desktop, account -create -ui,", gfx_cursor_x, gfx_cursor_y, 0x00FFFF00, 0x00000000);
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
}

// Handler für Login-Screen (mit fixem ESC)
void handle_login_key(char c) {
    if (c == 0x1B) { // ESC
        system_mode = 0;
        clear_screen_graphics(0x00000000);
        draw_string("VeloOS Kernel - Bare Metal Mode Active", 50, 30, 0x00FFFFFF, 0x00000000);
        draw_string("Zurueck in der Shell.", 50, 60, 0x0000FFCC, 0x00000000);
        gfx_cursor_x = 50; gfx_cursor_y = 120;
        draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
        gfx_cursor_x += 32;
        return;
    }
    if (c == '\t') {
        login_field_focus = (login_field_focus + 1) % 3;
    } else if (c == '\n' || c == '\r') {
        if (login_field_focus < 2) {
            login_field_focus++;
        } else {
            if (verify_login(login_username, login_password)) {
                // Sensible RAM-Daten sofort löschen
                __builtin_memset(login_password, 0, 32);
                system_mode = 3; 
                show_desktop();
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

// Handler für Account Setup Wizard (mit ESC-Abbruch)
void handle_account_ui_key(char c) {
    if (c == 0x1B) { // ESC - Wizard abbrechen
        system_mode = 0;
        clear_screen_graphics(0x00000000);
        draw_string("VeloOS Kernel - Bare Metal Mode Active", 50, 30, 0x00FFFFFF, 0x00000000);
        draw_string("Account Setup abgebrochen.", 50, 60, 0x00FF0000, 0x00000000);
        gfx_cursor_x = 50; gfx_cursor_y = 120;
        draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
        gfx_cursor_x += 32;
        return;
    }
    if (c == '\t') {
        ui_field_focus = (ui_field_focus + 1) % 3;
    } else if (c == '\n' || c == '\r') {
        if (ui_field_focus < 2) {
            ui_field_focus++;
        } else {
            save_account_to_disk(ui_username, ui_password);
            // Sensible Daten aus dem RAM löschen
            __builtin_memset(ui_password, 0, 32);
            __builtin_memset(ui_username, 0, 32);
            
            system_mode = 0; 
            clear_screen_graphics(0x00000000);
            draw_string("VeloOS Kernel - Bare Metal Mode Active", 50, 30, 0x00FFFFFF, 0x00000000);
            draw_string("Account erfolgreich auf Disk erstellt & gesichert!", 50, 60, 0x0000FFCC, 0x00000000);
            gfx_cursor_x = 50; gfx_cursor_y = 120;
            draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
            gfx_cursor_x += 32;
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

// Handler für den Desktop (inklusive F1 Startmenü & ESC Logout)
void handle_desktop_key(char c) {
    if (c == 0x1B) { // ESC -> Logout zur Shell
        desktop_menu_open = 0;
        system_mode = 0;
        clear_screen_graphics(0x00000000);
        draw_string("VeloOS Kernel - Bare Metal Mode Active", 50, 30, 0x00FFFFFF, 0x00000000);
        draw_string("Abgemeldet. Zurueck in der Shell.", 50, 60, 0x0000FFCC, 0x00000000);
        gfx_cursor_x = 50; gfx_cursor_y = 120;
        draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
        gfx_cursor_x += 32;
        return;
    }
    
    // Prüfe auf Funktionstaste F1 (Scan-Code / ASCII-Mapping je nach keyboard.h, oft blockiert oder als Sonderzeichen abgefangen)
    // Wenn dein Keyboard-Treiber F1 als spezifisches Zeichen oder via Scan-Code liefert, hier einbauen. 
    // Falls F1 als ASCII 0 (oder ein spezieller Key) reinkommt:
    if (c == 0x3B || c == 0xF1) { // F1 Key (je nach Implementierung in keyboard.h)
        desktop_menu_open = !desktop_menu_open;
        show_desktop();
        return;
    }

    if (c == 0x09) { // TAB
        if (desktop_menu_open) desktop_menu_focus = (desktop_menu_focus + 1) % 2;
    } else if (c == '\n' || c == '\r') {
        if (desktop_menu_open) {
            if (desktop_menu_focus == 1 || desktop_menu_focus == 0) { 
                desktop_menu_open = 0;
                system_mode = 0;
                clear_screen_graphics(0x00000000);
                draw_string("VeloOS Kernel - Bare Metal Mode Active", 50, 30, 0x00FFFFFF, 0x00000000);
                draw_string("Abgemeldet.", 50, 60, 0x0000FFCC, 0x00000000);
                gfx_cursor_x = 50; gfx_cursor_y = 120;
                draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
                gfx_cursor_x += 32;
                return;
            }
        } else {
            desktop_menu_open = 1;
        }
    }
    show_desktop();
}

void handle_shell_key(char c) {
    if (c == '\n' || c == '\r') execute_command();
    else if (c == '\b') {
        if (command_length > 0 && gfx_cursor_x > 82) {
            command_length--;
            gfx_cursor_x -= 16;
            draw_char(' ', gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
        }
    } else if (c >= 32 && c <= 126) {
        if (command_length < 61) {
            command_buffer[command_length++] = c;
            draw_char(c, gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
            gfx_cursor_x += 16;
        }
    }
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    g_st = SystemTable;
    g_image_handle = ImageHandle;
    InitializeLib(ImageHandle, SystemTable);

    init_gop();
    discover_ahci_bar_direct();
    clear_screen_graphics(0x00000000);

    draw_string("VeloOS Bootloader aktiv. Tippe 'exit' fuer Bare-Metal.", 50, 50, 0x0000FFCC, 0x00000000);
    gfx_cursor_x = 50; gfx_cursor_y = 100;
    draw_string("> ", gfx_cursor_x, gfx_cursor_y, 0x00FFFFFF, 0x00000000);
    gfx_cursor_x += 32;

    init_keyboard();

    while (1) {
        char ascii = poll_keyboard_ascii();
        if (ascii != 0) {
            if (system_mode == 1) handle_login_key(ascii);         
            else if (system_mode == 2) handle_account_ui_key(ascii); 
            else if (system_mode == 3) handle_desktop_key(ascii);    
            else handle_shell_key(ascii);                            
        }
    }
    return EFI_SUCCESS;
}
#include "mouse.h"

extern UINTN gop_width;
extern UINTN gop_height;

void put_pixel(UINTN x, UINTN y, UINT32 color);

static MouseState g_mouse = {0};
static int g_prev_left = 0;
static int g_prev_right = 0;
static int g_mouse_cycle = 0;
static unsigned char g_mouse_packet[4];
static int g_is_wheel_mouse = 0;

/* =========================================================================
 * HARDWARE PORT I/O (INTELLIMOUSE 4-BYTE MIT MAUSRAD)
 * ========================================================================= */
static inline unsigned char inb_kbc(unsigned short port) {
    unsigned char res;
    __asm__ volatile("inb %1, %0" : "=a"(res) : "Nd"(port));
    return res;
}

static inline void outb_kbc(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void kbc_wait_write(void) {
    int timeout = 10000;
    while ((inb_kbc(0x64) & 0x02) && --timeout) {
        __asm__ volatile("pause");
    }
}

static inline void kbc_wait_read(void) {
    int timeout = 10000;
    while (!(inb_kbc(0x64) & 0x01) && --timeout) {
        __asm__ volatile("pause");
    }
}

static void ps2_mouse_write(unsigned char val) {
    kbc_wait_write();
    outb_kbc(0x64, 0xD4);
    kbc_wait_write();
    outb_kbc(0x60, val);
    kbc_wait_read();
    if (inb_kbc(0x64) & 0x01) {
        inb_kbc(0x60);
    }
}

/* =========================================================================
 * UEFI USB POINTER PROTOKOLLE
 * ========================================================================= */
#define MAX_POINTER_HANDLES 8

static EFI_GUID g_simple_pointer_guid = EFI_SIMPLE_POINTER_PROTOCOL_GUID;
static EFI_GUID g_velo_abs_pointer_guid = {
    0x8D59D32B, 0xC655, 0x4AE9,
    { 0x9B, 0x15, 0xF2, 0x59, 0x04, 0x99, 0x2A, 0x43 }
};

typedef struct {
    UINT64 CurrentX;
    UINT64 CurrentY;
    UINT64 CurrentZ;
    UINT32 ActiveButtons;
} VELO_ABS_POINTER_STATE;

typedef struct {
    UINT64 AbsoluteMinX;
    UINT64 AbsoluteMinY;
    UINT64 AbsoluteMinZ;
    UINT64 AbsoluteMaxX;
    UINT64 AbsoluteMaxY;
    UINT64 AbsoluteMaxZ;
    UINT32 Attributes;
} VELO_ABS_POINTER_MODE;

typedef struct _VELO_ABS_POINTER_PROTOCOL VELO_ABS_POINTER_PROTOCOL;

struct _VELO_ABS_POINTER_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(VELO_ABS_POINTER_PROTOCOL *This, BOOLEAN ExtendedVerification);
    EFI_STATUS (EFIAPI *GetState)(VELO_ABS_POINTER_PROTOCOL *This, VELO_ABS_POINTER_STATE *State);
    EFI_EVENT  WaitForInput;
    VELO_ABS_POINTER_MODE *Mode;
};

static EFI_SIMPLE_POINTER_PROTOCOL *g_simple_pointers[MAX_POINTER_HANDLES];
static UINTN                       g_simple_pointer_count = 0;

static VELO_ABS_POINTER_PROTOCOL   *g_absolute_pointers[MAX_POINTER_HANDLES];
static UINTN                       g_absolute_pointer_count = 0;

static UINT64 g_prev_abs_z[MAX_POINTER_HANDLES] = {0};
static int    g_abs_z_init[MAX_POINTER_HANDLES] = {0};

/* Cursor Bitmaps */
static const unsigned char cursor_arrow[19][12] = {
    {2,0,0,0,0,0,0,0,0,0,0,0},
    {2,2,0,0,0,0,0,0,0,0,0,0},
    {2,1,2,0,0,0,0,0,0,0,0,0},
    {2,1,1,2,0,0,0,0,0,0,0,0},
    {2,1,1,1,2,0,0,0,0,0,0,0},
    {2,1,1,1,1,2,0,0,0,0,0,0},
    {2,1,1,1,1,1,2,0,0,0,0,0},
    {2,1,1,1,1,1,1,2,0,0,0,0},
    {2,1,1,1,1,1,1,1,2,0,0,0},
    {2,1,1,1,1,1,1,1,1,2,0,0},
    {2,1,1,1,1,1,2,2,2,2,0,0},
    {2,1,1,2,1,1,2,0,0,0,0,0},
    {2,1,2,0,2,1,1,2,0,0,0,0},
    {2,2,0,0,2,1,1,2,0,0,0,0},
    {2,0,0,0,0,2,1,1,2,0,0,0},
    {0,0,0,0,0,2,1,1,2,0,0,0},
    {0,0,0,0,0,0,2,1,1,2,0,0},
    {0,0,0,0,0,0,2,1,1,2,0,0},
    {0,0,0,0,0,0,0,2,2,0,0,0}
};

static const unsigned char cursor_hand[18][14] = {
    {0,0,0,0,2,2,0,0,0,0,0,0,0,0},
    {0,0,0,2,1,1,2,0,0,0,0,0,0,0},
    {0,0,0,2,1,1,2,0,0,0,0,0,0,0},
    {0,0,0,2,1,1,2,0,0,0,0,0,0,0},
    {0,0,0,2,1,1,2,0,0,0,0,0,0,0},
    {0,0,0,2,1,1,2,2,2,0,0,0,0,0},
    {0,2,2,2,1,1,2,1,1,2,2,0,0,0},
    {2,1,1,2,1,1,2,1,1,2,1,2,0,0},
    {2,1,1,2,1,1,2,1,1,2,1,1,2,0},
    {0,2,1,1,2,1,1,1,1,2,1,1,2,0},
    {0,2,1,1,1,1,1,1,1,1,1,1,2,0},
    {0,0,2,1,1,1,1,1,1,1,1,1,2,0},
    {0,0,2,1,1,1,1,1,1,1,1,1,2,0},
    {0,0,0,2,1,1,1,1,1,1,2,0,0,0},
    {0,0,0,2,1,1,1,1,1,2,0,0,0,0},
    {0,0,0,0,2,1,1,1,1,2,0,0,0,0},
    {0,0,0,0,2,1,1,1,1,2,0,0,0,0},
    {0,0,0,0,0,2,2,2,2,0,0,0,0,0}
};

static const unsigned char cursor_ibeam[15][7] = {
    {2,2,2,2,2,2,2},
    {2,1,1,1,1,1,2},
    {0,2,2,1,2,2,0},
    {0,0,2,1,2,0,0},
    {0,0,2,1,2,0,0},
    {0,0,2,1,2,0,0},
    {0,0,2,1,2,0,0},
    {0,0,2,1,2,0,0},
    {0,0,2,1,2,0,0},
    {0,0,2,1,2,0,0},
    {0,0,2,1,2,0,0},
    {0,0,2,1,2,0,0},
    {0,2,2,1,2,2,0},
    {2,1,1,1,1,1,2},
    {2,2,2,2,2,2,2}
};

void mouse_init(void) {
    g_mouse.x = (int)gop_width / 2;
    g_mouse.y = (int)gop_height / 2;
    g_mouse.left_button = 0;
    g_mouse.right_button = 0;
    g_mouse.middle_button = 0;
    g_mouse.left_clicked = 0;
    g_mouse.right_clicked = 0;
    g_mouse.left_released = 0;
    g_mouse.scroll_z = 0;
    g_mouse.scroll_h = 0;
    g_mouse.cursor_type = CURSOR_ARROW;
    g_prev_left = 0;
    g_prev_right = 0;
    g_mouse_cycle = 0;

    // 1. UEFI Pointer Protocols lokalisieren
    g_simple_pointer_count = 0;
    g_absolute_pointer_count = 0;

    for (int i = 0; i < MAX_POINTER_HANDLES; i++) {
        g_abs_z_init[i] = 0;
        g_prev_abs_z[i] = 0;
    }

    if (BS) {
        UINTN handle_count = 0;
        EFI_HANDLE *handles = NULL;
        EFI_STATUS status = uefi_call_wrapper(BS->LocateHandleBuffer, 5,
                                              ByProtocol, &g_simple_pointer_guid,
                                              NULL, &handle_count, &handles);

        if (!EFI_ERROR(status) && handles) {
            for (UINTN i = 0; i < handle_count && g_simple_pointer_count < MAX_POINTER_HANDLES; i++) {
                EFI_SIMPLE_POINTER_PROTOCOL *ptr = NULL;
                status = uefi_call_wrapper(BS->HandleProtocol, 3, handles[i], &g_simple_pointer_guid, (void**)&ptr);
                if (!EFI_ERROR(status) && ptr) {
                    uefi_call_wrapper(ptr->Reset, 2, ptr, FALSE);
                    g_simple_pointers[g_simple_pointer_count++] = ptr;
                }
            }
            uefi_call_wrapper(BS->FreePool, 1, handles);
        }

        handle_count = 0;
        handles = NULL;
        status = uefi_call_wrapper(BS->LocateHandleBuffer, 5,
                                   ByProtocol, &g_velo_abs_pointer_guid,
                                   NULL, &handle_count, &handles);

        if (!EFI_ERROR(status) && handles) {
            for (UINTN i = 0; i < handle_count && g_absolute_pointer_count < MAX_POINTER_HANDLES; i++) {
                VELO_ABS_POINTER_PROTOCOL *aptr = NULL;
                status = uefi_call_wrapper(BS->HandleProtocol, 3, handles[i], &g_velo_abs_pointer_guid, (void**)&aptr);
                if (!EFI_ERROR(status) && aptr) {
                    uefi_call_wrapper(aptr->Reset, 2, aptr, FALSE);
                    g_absolute_pointers[g_absolute_pointer_count++] = aptr;
                }
            }
            uefi_call_wrapper(BS->FreePool, 1, handles);
        }
    }

    // 2. Hardware Controller (KBC / USB Legacy) sauber konfigurieren (Keyboard aktiv halten!)
    kbc_wait_write();
    outb_kbc(0x64, 0xA8); // Enable Aux (Maus)
    kbc_wait_write();
    outb_kbc(0x64, 0xAE); // Enable Keyboard Interface

    kbc_wait_write();
    outb_kbc(0x64, 0x20); // Read Command Byte
    kbc_wait_read();
    unsigned char cmd = inb_kbc(0x60);
    cmd |= (1u << 0);  // Keyboard IRQ1 enable
    cmd |= (1u << 1);  // Mouse IRQ12 enable
    cmd &= ~(1u << 4); // Keyboard clock enable (clear disable bit)
    cmd &= ~(1u << 5); // Mouse clock enable (clear disable bit)
    kbc_wait_write();
    outb_kbc(0x64, 0x60);
    kbc_wait_write();
    outb_kbc(0x60, cmd);

    ps2_mouse_write(0xF6); // Set defaults

    // INTELLIMOUSE KNOCK SEQUENCE (Schaltet das 4. Byte für das Rad frei)
    ps2_mouse_write(0xF3); ps2_mouse_write(200);
    ps2_mouse_write(0xF3); ps2_mouse_write(100);
    ps2_mouse_write(0xF3); ps2_mouse_write(80);

    // Device ID prüfen
    ps2_mouse_write(0xF2);
    unsigned char dev_id = 0;
    if (inb_kbc(0x64) & 0x01) {
        dev_id = inb_kbc(0x60);
    }
    g_is_wheel_mouse = (dev_id == 3 || dev_id == 4);

    ps2_mouse_write(0xF4); // Enable data reporting
}

void mouse_set_cursor(int cursor_type) {
    g_mouse.cursor_type = cursor_type;
}

int mouse_update(void) {
    int moved = 0;
    g_mouse.left_clicked = 0;
    g_mouse.right_clicked = 0;
    g_mouse.left_released = 0;
    g_mouse.scroll_z = 0;
    g_mouse.scroll_h = 0;

    // --- 1. UEFI USB Relative Pointer ---
    for (UINTN i = 0; i < g_simple_pointer_count; i++) {
        EFI_SIMPLE_POINTER_PROTOCOL *p = g_simple_pointers[i];
        if (!p) continue;

        EFI_SIMPLE_POINTER_STATE state;
        EFI_STATUS st = uefi_call_wrapper(p->GetState, 2, p, &state);
        if (!EFI_ERROR(st)) {
            int dx = (int)state.RelativeMovementX;
            int dy = (int)state.RelativeMovementY;
            if (dx > 400 || dx < -400 || dy > 400 || dy < -400) { dx /= 64; dy /= 64; }
            if (dx != 0 || dy != 0) { g_mouse.x += dx; g_mouse.y += dy; moved = 1; }

            if (state.RelativeMovementZ != 0) {
                g_mouse.scroll_z = (state.RelativeMovementZ > 0) ? 1 : -1;
                moved = 1;
            }
            g_mouse.left_button = state.LeftButton ? 1 : 0;
            g_mouse.right_button = state.RightButton ? 1 : 0;
        }
    }

    // --- 2. UEFI USB Absolute Pointer (Tablet) ---
    for (UINTN i = 0; i < g_absolute_pointer_count; i++) {
        VELO_ABS_POINTER_PROTOCOL *ap = g_absolute_pointers[i];
        if (!ap || !ap->Mode) continue;

        VELO_ABS_POINTER_STATE astate;
        EFI_STATUS st = uefi_call_wrapper(ap->GetState, 2, ap, &astate);
        if (!EFI_ERROR(st)) {
            UINT64 range_x = ap->Mode->AbsoluteMaxX - ap->Mode->AbsoluteMinX;
            UINT64 range_y = ap->Mode->AbsoluteMaxY - ap->Mode->AbsoluteMinY;

            if (range_x > 0 && range_y > 0) {
                int new_x = (int)(((astate.CurrentX - ap->Mode->AbsoluteMinX) * gop_width) / range_x);
                int new_y = (int)(((astate.CurrentY - ap->Mode->AbsoluteMinY) * gop_height) / range_y);
                if (new_x != g_mouse.x || new_y != g_mouse.y) {
                    g_mouse.x = new_x; g_mouse.y = new_y; moved = 1;
                }
            }

            if (!g_abs_z_init[i]) {
                g_prev_abs_z[i] = astate.CurrentZ;
                g_abs_z_init[i] = 1;
            } else if (astate.CurrentZ != g_prev_abs_z[i]) {
                INT64 dz = (INT64)astate.CurrentZ - (INT64)g_prev_abs_z[i];
                g_prev_abs_z[i] = astate.CurrentZ;
                if (dz > 0) g_mouse.scroll_z = 1;
                else if (dz < 0) g_mouse.scroll_z = -1;
                moved = 1;
            }

            if (astate.ActiveButtons & 0x08) { g_mouse.scroll_z = 1; moved = 1; }
            else if (astate.ActiveButtons & 0x10) { g_mouse.scroll_z = -1; moved = 1; }

            g_mouse.left_button = (astate.ActiveButtons & 0x01) ? 1 : 0;
            g_mouse.right_button = (astate.ActiveButtons & 0x02) ? 1 : 0;
            g_mouse.middle_button = (astate.ActiveButtons & 0x04) ? 1 : 0;
        }
    }

    // --- 3. Direkte Hardware Port I/O (IntelliMouse 4-Byte Modus mit echtem Rad) ---
    int max_bytes = 16;
    while (max_bytes-- > 0) {
        unsigned char stat = inb_kbc(0x64);
        if (!(stat & 0x01) || !(stat & 0x20)) break;

        unsigned char b = inb_kbc(0x60);
        if (g_mouse_cycle == 0) {
            if (b & 0x08) { g_mouse_packet[0] = b; g_mouse_cycle = 1; }
        } else if (g_mouse_cycle == 1) {
            g_mouse_packet[1] = b; g_mouse_cycle = 2;
        } else if (g_mouse_cycle == 2) {
            g_mouse_packet[2] = b;
            if (g_is_wheel_mouse) {
                g_mouse_cycle = 3;
            } else {
                g_mouse_cycle = 0;
                int dx = (int)g_mouse_packet[1];
                int dy = (int)g_mouse_packet[2];
                if (g_mouse_packet[0] & 0x10) dx -= 256;
                if (g_mouse_packet[0] & 0x20) dy -= 256;
                if (!(g_mouse_packet[0] & 0xC0)) { g_mouse.x += dx; g_mouse.y -= dy; moved = 1; }
                g_mouse.left_button = (g_mouse_packet[0] & 0x01) ? 1 : 0;
                g_mouse.right_button = (g_mouse_packet[0] & 0x02) ? 1 : 0;
            }
        } else if (g_mouse_cycle == 3) {
            g_mouse_packet[3] = b;
            g_mouse_cycle = 0;

            int dx = (int)g_mouse_packet[1];
            int dy = (int)g_mouse_packet[2];
            if (g_mouse_packet[0] & 0x10) dx -= 256;
            if (g_mouse_packet[0] & 0x20) dy -= 256;

            if (!(g_mouse_packet[0] & 0xC0)) { g_mouse.x += dx; g_mouse.y -= dy; moved = 1; }

            // Byte 3 = Radbewegung (Vorzeichenbehaftet!)
            signed char dz = (signed char)g_mouse_packet[3];
            if (dz > 0) { g_mouse.scroll_z = 1; moved = 1; }
            else if (dz < 0) { g_mouse.scroll_z = -1; moved = 1; }

            g_mouse.left_button = (g_mouse_packet[0] & 0x01) ? 1 : 0;
            g_mouse.right_button = (g_mouse_packet[0] & 0x02) ? 1 : 0;
            g_mouse.middle_button = (g_mouse_packet[0] & 0x04) ? 1 : 0;
        }
    }

    if (g_mouse.x < 0) g_mouse.x = 0;
    if (g_mouse.y < 0) g_mouse.y = 0;
    if (g_mouse.x >= (int)gop_width) g_mouse.x = (int)gop_width - 1;
    if (g_mouse.y >= (int)gop_height) g_mouse.y = (int)gop_height - 1;

    if (g_mouse.left_button && !g_prev_left) { g_mouse.left_clicked = 1; moved = 1; }
    if (!g_mouse.left_button && g_prev_left) { g_mouse.left_released = 1; moved = 1; }
    if (g_mouse.right_button && !g_prev_right) { g_mouse.right_clicked = 1; moved = 1; }

    g_prev_left = g_mouse.left_button;
    g_prev_right = g_mouse.right_button;

    return moved;
}

MouseState* mouse_get_state(void) {
    return &g_mouse;
}

void mouse_draw_cursor(void) {
    int mx = g_mouse.x;
    int my = g_mouse.y;

    if (g_mouse.cursor_type == CURSOR_HAND) {
        for (int y = 0; y < 18; y++) {
            int py = my + y;
            if (py < 0 || py >= (int)gop_height) continue;
            for (int x = 0; x < 14; x++) {
                int px = mx + x;
                if (px < 0 || px >= (int)gop_width) continue;
                unsigned char p = cursor_hand[y][x];
                if (p == 1) put_pixel((UINTN)px, (UINTN)py, 0x00FFFFFF);
                else if (p == 2) put_pixel((UINTN)px, (UINTN)py, 0x00000000);
            }
        }
    } else if (g_mouse.cursor_type == CURSOR_IBEAM) {
        for (int y = 0; y < 15; y++) {
            int py = my - 7 + y;
            if (py < 0 || py >= (int)gop_height) continue;
            for (int x = 0; x < 7; x++) {
                int px = mx - 3 + x;
                if (px < 0 || px >= (int)gop_width) continue;
                unsigned char p = cursor_ibeam[y][x];
                if (p == 1) put_pixel((UINTN)px, (UINTN)py, 0x00FFFFFF);
                else if (p == 2) put_pixel((UINTN)px, (UINTN)py, 0x00000000);
            }
        }
    } else {
        for (int y = 0; y < 19; y++) {
            int py = my + y;
            if (py < 0 || py >= (int)gop_height) continue;
            for (int x = 0; x < 12; x++) {
                int px = mx + x;
                if (px < 0 || px >= (int)gop_width) continue;
                unsigned char p = cursor_arrow[y][x];
                if (p == 1) put_pixel((UINTN)px, (UINTN)py, 0x00FFFFFF);
                else if (p == 2) put_pixel((UINTN)px, (UINTN)py, 0x00000000);
            }
        }
    }
}
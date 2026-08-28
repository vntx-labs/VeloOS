#include "mouse.h"

extern UINTN gop_width;
extern UINTN gop_height;

void put_pixel(UINTN x, UINTN y, UINT32 color);

#define MAX_POINTER_DEVICES 8
static EFI_SIMPLE_POINTER_PROTOCOL *g_pointers[MAX_POINTER_DEVICES];
static int g_pointer_count = 0;

static MouseState g_mouse = {0};
static int g_prev_left = 0;
static int g_prev_right = 0;
static int g_mouse_cycle = 0;
static unsigned char g_mouse_packet[4];
static int g_is_scroll_mouse = 0;

static inline unsigned char inb_kbc(unsigned short port) {
    unsigned char res;
    __asm__ volatile("inb %1, %0" : "=a"(res) : "Nd"(port));
    return res;
}

static inline void outb_kbc(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

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
    {0,0,2,1,1,1,1,1,1,1,1,2,0,0},
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

static void ps2_mouse_write(unsigned char val) {
    outb_kbc(0x64, 0xD4);
    outb_kbc(0x60, val);
    for (int i = 0; i < 1000; i++) {
        if (inb_kbc(0x64) & 0x01) {
            inb_kbc(0x60);
            break;
        }
    }
}

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
    g_pointer_count = 0;

    // 1. UEFI Pointer Protocols
    if (BS && BS->LocateHandleBuffer) {
        UINTN num_handles = 0;
        EFI_HANDLE *handles = NULL;
        EFI_GUID ptr_guid = EFI_SIMPLE_POINTER_PROTOCOL_GUID;

        EFI_STATUS status = uefi_call_wrapper(BS->LocateHandleBuffer, 5, 
            ByProtocol, &ptr_guid, NULL, &num_handles, &handles);

        if (status == EFI_SUCCESS && handles && num_handles > 0) {
            for (UINTN i = 0; i < num_handles && g_pointer_count < MAX_POINTER_DEVICES; i++) {
                EFI_SIMPLE_POINTER_PROTOCOL *p = NULL;
                status = uefi_call_wrapper(BS->HandleProtocol, 3, handles[i], &ptr_guid, (VOID**)&p);
                if (status == EFI_SUCCESS && p) {
                    if (p->Reset) uefi_call_wrapper(p->Reset, 2, p, FALSE);
                    g_pointers[g_pointer_count++] = p;
                }
            }
            uefi_call_wrapper(BS->FreePool, 1, handles);
        }
    }

    // 2. PS/2 Scroll-Maus Erkennung & Aktivierung (IntelliMouse 4-Byte Modus)
    outb_kbc(0x64, 0xA8);
    outb_kbc(0x64, 0x20);
    for (int i = 0; i < 50; i++) {
        if (inb_kbc(0x64) & 0x01) {
            unsigned char status = inb_kbc(0x60);
            status &= ~(1u << 5);
            status |= (1u << 1) | (1u << 6);
            outb_kbc(0x64, 0x60);
            outb_kbc(0x60, status);
            break;
        }
    }

    // IntelliMouse Magic Sequence für 4D-Mausrad (vertikal + horizontal)
    ps2_mouse_write(0xF3); ps2_mouse_write(200);
    ps2_mouse_write(0xF3); ps2_mouse_write(100);
    ps2_mouse_write(0xF3); ps2_mouse_write(80);
    ps2_mouse_write(0xF2);
    g_is_scroll_mouse = 1;

    ps2_mouse_write(0xF4); // Enable packet streaming
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

    // 1. UEFI Pointer
    for (int i = 0; i < g_pointer_count; i++) {
        EFI_SIMPLE_POINTER_PROTOCOL *p = g_pointers[i];
        if (!p || !p->GetState) continue;

        EFI_SIMPLE_POINTER_STATE state;
        EFI_STATUS status = uefi_call_wrapper(p->GetState, 2, p, &state);
        if (status == EFI_SUCCESS) {
            INT32 dx = state.RelativeMovementX;
            INT32 dy = state.RelativeMovementY;
            INT32 dz = state.RelativeMovementZ;

            if (p->Mode && p->Mode->ResolutionX > 0 && p->Mode->ResolutionY > 0) {
                dx = (INT32)(((INT64)dx * 3) / (INT64)p->Mode->ResolutionX);
                dy = (INT32)(((INT64)dy * 3) / (INT64)p->Mode->ResolutionY);
            }

            if (dx != 0 || dy != 0) {
                g_mouse.x += dx;
                g_mouse.y += dy;
                moved = 1;
            }

            if (dz != 0) {
                g_mouse.scroll_z = (dz < 0) ? 1 : -1;
                moved = 1;
            }

            g_mouse.left_button = state.LeftButton ? 1 : 0;
            g_mouse.right_button = state.RightButton ? 1 : 0;
        }
    }

    // 2. PS/2 Hardware-Maus mit 4-Byte Packet Handling (Scrollrad)
    int max_bytes = 16;
    while (max_bytes-- > 0) {
        unsigned char status = inb_kbc(0x64);
        if (!(status & 0x01) || !(status & 0x20)) break;

        unsigned char b = inb_kbc(0x60);
        if (g_mouse_cycle == 0) {
            if (b & 0x08) {
                g_mouse_packet[0] = b;
                g_mouse_cycle = 1;
            }
        } else if (g_mouse_cycle == 1) {
            g_mouse_packet[1] = b;
            g_mouse_cycle = 2;
        } else if (g_mouse_cycle == 2) {
            g_mouse_packet[2] = b;
            if (g_is_scroll_mouse) {
                g_mouse_cycle = 3;
            } else {
                g_mouse_cycle = 0;
                int dx = (int)g_mouse_packet[1];
                int dy = (int)g_mouse_packet[2];
                if (g_mouse_packet[0] & 0x10) dx -= 256;
                if (g_mouse_packet[0] & 0x20) dy -= 256;
                if (!(g_mouse_packet[0] & 0xC0)) {
                    g_mouse.x += dx;
                    g_mouse.y -= dy;
                    moved = 1;
                }
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

            if (!(g_mouse_packet[0] & 0xC0)) {
                g_mouse.x += dx;
                g_mouse.y -= dy;
                moved = 1;
            }

            g_mouse.left_button = (g_mouse_packet[0] & 0x01) ? 1 : 0;
            g_mouse.right_button = (g_mouse_packet[0] & 0x02) ? 1 : 0;
            g_mouse.middle_button = (g_mouse_packet[0] & 0x04) ? 1 : 0;

            // Vertikales & Horizontales Rad aus Byte 4
            signed char wheel = (signed char)(g_mouse_packet[3] & 0x0F);
            if (wheel & 0x08) wheel |= 0xF0; // Sign extend 4-bit

            if (wheel > 0) { g_mouse.scroll_z = 1; moved = 1; }
            else if (wheel < 0) { g_mouse.scroll_z = -1; moved = 1; }

            // Horizontales Tilt-Wheel
            if ((g_mouse_packet[3] & 0x30) == 0x10) { g_mouse.scroll_h = 1; moved = 1; }
            else if ((g_mouse_packet[3] & 0x30) == 0x20) { g_mouse.scroll_h = -1; moved = 1; }
        }
    }

    if (g_mouse.x < 0) g_mouse.x = 0;
    if (g_mouse.y < 0) g_mouse.y = 0;
    if (g_mouse.x >= (int)gop_width) g_mouse.x = (int)gop_width - 1;
    if (g_mouse.y >= (int)gop_height) g_mouse.y = (int)gop_height - 1;

    if (g_mouse.left_button && !g_prev_left) {
        g_mouse.left_clicked = 1;
        moved = 1;
    }
    if (!g_mouse.left_button && g_prev_left) {
        g_mouse.left_released = 1;
        moved = 1;
    }
    if (g_mouse.right_button && !g_prev_right) {
        g_mouse.right_clicked = 1;
        moved = 1;
    }

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
                if (p == 1) put_pixel(px, py, 0x00FFFFFF);
                else if (p == 2) put_pixel(px, py, 0x00000000);
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
                if (p == 1) put_pixel(px, py, 0x00FFFFFF);
                else if (p == 2) put_pixel(px, py, 0x00000000);
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
                if (p == 1) put_pixel(px, py, 0x00FFFFFF);
                else if (p == 2) put_pixel(px, py, 0x00000000);
            }
        }
    }
}
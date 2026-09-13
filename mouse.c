#include "mouse.h"

extern UINTN gop_width;
extern UINTN gop_height;

void put_pixel(UINTN x, UINTN y, UINT32 color);

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
    kbc_wait_write();
    outb_kbc(0x64, 0xD4);
    kbc_wait_write();
    outb_kbc(0x60, val);
    kbc_wait_read();
    if (inb_kbc(0x64) & 0x01) {
        inb_kbc(0x60);
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

    kbc_wait_write();
    outb_kbc(0x64, 0xA8); // Aux port enable

    kbc_wait_write();
    outb_kbc(0x64, 0x20); // Read Compaq Status
    kbc_wait_read();
    unsigned char status = inb_kbc(0x60);
    status |= (1u << 1);  // Enable Mouse IRQ12
    status &= ~(1u << 5); // Disable Mouse Clock Disable
    kbc_wait_write();
    outb_kbc(0x64, 0x60);
    kbc_wait_write();
    outb_kbc(0x60, status);

    ps2_mouse_write(0xF6); // Set Defaults
    ps2_mouse_write(0xF4); // Enable Data Reporting
    g_is_scroll_mouse = 0;
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
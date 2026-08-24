// mouse.c - Hardware-Treiber für Bare-Metal & USB/PS2-Maus via i8042 Auxiliary Interface
#include "mouse.h"

extern UINTN gop_width;
extern UINTN gop_height;

void put_pixel(UINTN x, UINTN y, UINT32 color);

static inline unsigned char inb_kbc(unsigned short port) {
    unsigned char res;
    __asm__ volatile("inb %1, %0" : "=a"(res) : "Nd"(port));
    return res;
}

static inline void outb_kbc(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void io_delay(void) {
    outb_kbc(0x80, 0);
}

static MouseState g_mouse = {0};
static int g_prev_left = 0;
static int g_prev_right = 0;
static int g_mouse_cycle = 0;
static unsigned char g_mouse_packet[3];

/*
 * 12x19 Pixel High-Contrast Mauszeiger
 * 1 = Weiß, 2 = Schwarz, 0 = Transparent
 */
static const unsigned char cursor_pattern[19][12] = {
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

static void kbc_wait_write(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(inb_kbc(0x64) & 0x02)) return;
        io_delay();
    }
}

static void kbc_wait_read(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb_kbc(0x64) & 0x01) return;
        io_delay();
    }
}

static void mouse_write(unsigned char val) {
    kbc_wait_write();
    outb_kbc(0x64, 0xD4); // Signalisiert dem 8042: Nächstes Byte geht an die Maus
    kbc_wait_write();
    outb_kbc(0x60, val);
}

static unsigned char mouse_read(void) {
    kbc_wait_read();
    return inb_kbc(0x60);
}

void mouse_init(void) {
    g_mouse.x = (int)gop_width / 2;
    g_mouse.y = (int)gop_height / 2;
    g_mouse.left_button = 0;
    g_mouse.right_button = 0;
    g_mouse.left_clicked = 0;
    g_mouse.right_clicked = 0;
    g_prev_left = 0;
    g_prev_right = 0;
    g_mouse_cycle = 0;

    // 1. Auxiliary-Port (Maus) am 8042-Controller aktivieren
    kbc_wait_write();
    outb_kbc(0x64, 0xA8);

    // 2. Controller-Konfiguration lesen
    kbc_wait_write();
    outb_kbc(0x64, 0x20);
    kbc_wait_read();
    unsigned char status = inb_kbc(0x60);

    // Mouse-Clock einschalten (Bit 5 = 0), IRQ12 aus für Polling (Bit 1 = 0)
    status &= ~(1u << 5);
    status &= ~(1u << 1);
    status |= (1u << 6); // Scancode Translation

    kbc_wait_write();
    outb_kbc(0x64, 0x60);
    kbc_wait_write();
    outb_kbc(0x60, status);

    // 3. Maus-Befehle senden: Defaults laden (0xF6) & Daten-Reporting aktivieren (0xF4)
    mouse_write(0xF6);
    (void)mouse_read(); // ACK (0xFA)

    mouse_write(0xF4);
    (void)mouse_read(); // ACK (0xFA)
}

int mouse_update(void) {
    int moved = 0;
    g_mouse.left_clicked = 0;
    g_mouse.right_clicked = 0;

    /* Liest anstehende Maus-Bytes aus dem Puffer */
    while (inb_kbc(0x64) & 0x01) {
        unsigned char status = inb_kbc(0x64);
        
        // Wenn Bit 5 (0x20) NICHT gesetzt ist, sind es Tastaturdaten -> nicht verbrauchen
        if (!(status & 0x20)) {
            break;
        }

        unsigned char b = inb_kbc(0x60);

        if (g_mouse_cycle == 0) {
            // Byte 0 muss immer Bit 3 gesetzt haben (Sync-Bit)
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

            unsigned char flags = g_mouse_packet[0];
            int dx = (int)g_mouse_packet[1];
            int dy = (int)g_mouse_packet[2];

            // Vorzeichenerweiterung (9-Bit Vorzeichen aus Byte 0)
            if (flags & 0x10) dx -= 256;
            if (flags & 0x20) dy -= 256;

            // Overflow prüfen
            if (!(flags & 0xC0)) {
                if (dx != 0 || dy != 0) {
                    g_mouse.x += dx;
                    g_mouse.y -= dy; // Y-Achse invertieren (PS/2 liefert positiv nach oben)
                    moved = 1;
                }
            }

            g_mouse.left_button = (flags & 0x01) ? 1 : 0;
            g_mouse.right_button = (flags & 0x02) ? 1 : 0;
        }
    }

    // Bildschirm-Grenzen einhalten
    if (g_mouse.x < 0) g_mouse.x = 0;
    if (g_mouse.y < 0) g_mouse.y = 0;
    if (g_mouse.x >= (int)gop_width) g_mouse.x = (int)gop_width - 1;
    if (g_mouse.y >= (int)gop_height) g_mouse.y = (int)gop_height - 1;

    // Klick-Events bei Flankenwechsel
    if (g_mouse.left_button && !g_prev_left) {
        g_mouse.left_clicked = 1;
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

    for (int y = 0; y < 19; y++) {
        int py = my + y;
        if (py < 0 || py >= (int)gop_height) continue;

        for (int x = 0; x < 12; x++) {
            int px = mx + x;
            if (px < 0 || px >= (int)gop_width) continue;

            unsigned char pixel = cursor_pattern[y][x];
            if (pixel == 1) {
                put_pixel(px, py, 0x00FFFFFF); // Weiß
            } else if (pixel == 2) {
                put_pixel(px, py, 0x00000000); // Schwarzer Rand
            }
        }
    }
}
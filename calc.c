// calc.c - VeloOS Taschenrechner App mit hohem Kontrast & brillanter UI
#include "calc.h"
#include "wm.h"
#include "font.h"

void draw_rounded_rect_aa(int sx, int sy, int w, int h, int r, UINT32 color);
void draw_rounded_rect_gradient(int sx, int sy, int w, int h, int r, UINT32 top_col, UINT32 bot_col);
void draw_filled_rect(UINTN start_x, UINTN start_y, UINTN width, UINTN height, UINT32 color);

typedef struct {
    int x, y, w, h;
    char label[4];
    UINT32 bg_top;
    UINT32 bg_bot;
    UINT32 border_col;
    UINT32 fg_color;
} CalcBtn;

static const CalcBtn g_buttons[] = {
    // Zeile 1
    {10,  62, 46, 36, "7", 0x0024283B, 0x00181A24, 0x00475569, 0x00FFFFFF},
    {62,  62, 46, 36, "8", 0x0024283B, 0x00181A24, 0x00475569, 0x00FFFFFF},
    {114, 62, 46, 36, "9", 0x0024283B, 0x00181A24, 0x00475569, 0x00FFFFFF},
    {166, 62, 46, 36, "/", 0x002563EB, 0x001D4ED8, 0x0060A5FA, 0x00FFFFFF},

    // Zeile 2
    {10, 104, 46, 36, "4", 0x0024283B, 0x00181A24, 0x00475569, 0x00FFFFFF},
    {62, 104, 46, 36, "5", 0x0024283B, 0x00181A24, 0x00475569, 0x00FFFFFF},
    {114, 104, 46, 36, "6", 0x0024283B, 0x00181A24, 0x00475569, 0x00FFFFFF},
    {166, 104, 46, 36, "*", 0x002563EB, 0x001D4ED8, 0x0060A5FA, 0x00FFFFFF},

    // Zeile 3
    {10, 146, 46, 36, "1", 0x0024283B, 0x00181A24, 0x00475569, 0x00FFFFFF},
    {62, 146, 46, 36, "2", 0x0024283B, 0x00181A24, 0x00475569, 0x00FFFFFF},
    {114, 146, 46, 36, "3", 0x0024283B, 0x00181A24, 0x00475569, 0x00FFFFFF},
    {166, 146, 46, 36, "-", 0x002563EB, 0x001D4ED8, 0x0060A5FA, 0x00FFFFFF},

    // Zeile 4
    {10, 188, 46, 36, "C", 0x00DC2626, 0x00991B1B, 0x00F87171, 0x00FFFFFF},
    {62, 188, 46, 36, "0", 0x0024283B, 0x00181A24, 0x00475569, 0x00FFFFFF},
    {114, 188, 46, 36, "=", 0x0016A34A, 0x0015803D, 0x004ADE80, 0x00FFFFFF},
    {166, 188, 46, 36, "+", 0x002563EB, 0x001D4ED8, 0x0060A5FA, 0x00FFFFFF}
};
#define NUM_CALC_BTNS 16

static char g_display[32] = "0";
static long long g_operand1 = 0;
static char g_current_op = 0;
static int g_reset_on_next = 0;

static void int_to_str(long long val, char *buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    int neg = 0;
    if (val < 0) { neg = 1; val = -val; }
    char tmp[32]; int i = 0;
    while (val > 0) {
        tmp[i++] = '0' + (char)(val % 10);
        val /= 10;
    }
    int pos = 0;
    if (neg) buf[pos++] = '-';
    while (i > 0) buf[pos++] = tmp[--i];
    buf[pos] = '\0';
}

static long long str_to_int(const char *s) {
    long long res = 0; int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') {
        res = res * 10 + (*s - '0');
        s++;
    }
    return neg ? -res : res;
}

static void calc_execute_op(char op) {
    if (op >= '0' && op <= '9') {
        if (g_reset_on_next || (g_display[0] == '0' && g_display[1] == '\0')) {
            g_display[0] = op;
            g_display[1] = '\0';
            g_reset_on_next = 0;
        } else {
            int len = 0; while (g_display[len]) len++;
            if (len < 16) {
                g_display[len] = op;
                g_display[len + 1] = '\0';
            }
        }
    } else if (op == 'C' || op == 'c') {
        g_display[0] = '0';
        g_display[1] = '\0';
        g_operand1 = 0;
        g_current_op = 0;
        g_reset_on_next = 0;
    } else if (op == '+' || op == '-' || op == '*' || op == '/') {
        g_operand1 = str_to_int(g_display);
        g_current_op = op;
        g_reset_on_next = 1;
    } else if (op == '=' || op == '\n' || op == '\r') {
        if (g_current_op) {
            long long operand2 = str_to_int(g_display);
            long long result = 0;
            if (g_current_op == '+') result = g_operand1 + operand2;
            else if (g_current_op == '-') result = g_operand1 - operand2;
            else if (g_current_op == '*') result = g_operand1 * operand2;
            else if (g_current_op == '/' && operand2 != 0) result = g_operand1 / operand2;
            int_to_str(result, g_display);
            g_current_op = 0;
            g_reset_on_next = 1;
        }
    }
}

/* Paint-Callback für den Window Manager */
static void calc_on_paint(int win_id, int cx, int cy, int cw, int ch) {
    (void)win_id; (void)cw; (void)ch;

    // 1. LCD Display Container (Tiefes Obsidian mit Cyan-Rahmen)
    draw_rounded_rect_aa(cx + 8, cy + 8, 206, 44, 6, 0x0038BDF8);
    draw_rounded_rect_aa(cx + 9, cy + 9, 204, 42, 5, 0x00020617);
    
    // Display-Text rechtsbündig
    int text_len = 0; while (g_display[text_len]) text_len++;
    int text_x = cx + 200 - (text_len * 8);
    wm_draw_text(g_display, text_x, cy + 22, 0x0038BDF8, 0x00020617);

    // 2. Buttons
    for (int i = 0; i < NUM_CALC_BTNS; i++) {
        int bx = cx + g_buttons[i].x;
        int by = cy + g_buttons[i].y;
        int bw = g_buttons[i].w;
        int bh = g_buttons[i].h;

        draw_rounded_rect_aa(bx, by, bw, bh, 6, g_buttons[i].border_col);
        draw_rounded_rect_gradient(bx + 1, by + 1, bw - 2, bh - 2, 5, g_buttons[i].bg_top, g_buttons[i].bg_bot);
        
        wm_draw_text(g_buttons[i].label, bx + 19, by + 11, g_buttons[i].fg_color, 0x00000000);
    }
}

/* Klick-Handler */
static void calc_on_click(int win_id, int lx, int ly) {
    (void)win_id;
    for (int i = 0; i < NUM_CALC_BTNS; i++) {
        if (lx >= g_buttons[i].x && lx <= g_buttons[i].x + g_buttons[i].w &&
            ly >= g_buttons[i].y && ly <= g_buttons[i].y + g_buttons[i].h) {
            calc_execute_op(g_buttons[i].label[0]);
            wm_mark_all_dirty();
            break;
        }
    }
}

/* Tastatur-Handler */
static void calc_on_key(int win_id, char key) {
    (void)win_id;
    calc_execute_op(key);
    wm_mark_all_dirty();
}

void calc_app_launch(void) {
    g_display[0] = '0';
    g_display[1] = '\0';
    g_operand1 = 0;
    g_current_op = 0;
    g_reset_on_next = 0;

    int win_id = wm_create_window_auto("Taschenrechner", 222, 234, calc_on_paint);
    if (win_id >= 0) {
        Window *win = wm_get_window(win_id);
        if (win) {
            win->on_click = calc_on_click;
            win->on_key = calc_on_key;
        }
    }
    wm_mark_all_dirty();
}
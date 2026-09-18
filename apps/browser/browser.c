#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <velo/window.h>
#include <velo/syscall.h>

#define WIN_DEFAULT_W 880
#define WIN_DEFAULT_H 600

#define TOP_TAB_HEIGHT    28
#define TOP_NAV_HEIGHT    38
#define TOP_CHROME_HEIGHT (TOP_TAB_HEIGHT + TOP_NAV_HEIGHT) // 66px
#define STATUS_BAR_HEIGHT 22

#define MAX_TABS 8

extern int velo_ui_textbox_handle_key(char *text, int max_len, int *cursor_pos, char key);

static int g_win_w = WIN_DEFAULT_W;
static int g_win_h = WIN_DEFAULT_H;
static velo_window_t g_browser_win = -1;

// Adressleiste & Navigation
static char g_url_input[256] = "about:home";
static int  g_url_cursor = 10;
static int  g_url_focused = 0;

typedef struct {
    int  active;
    char url[256];
    char title[64];
    char content[131072];
    char history[32][256];
    int  hist_count;
    int  hist_pos;
    int  scroll_y;
    int  max_scroll_y;
    int  is_loading_async;
    int  timeout_timer;
    char status_text[64];
} BrowserTab;

static BrowserTab g_tabs[MAX_TABS];
static int g_active_tab = 0;
static int g_tab_count = 1;

static const char* g_default_home_text =
"=== Velo Fox Web Browser ===\n\n"
"Willkommen in Ihrem nativen 64-Bit Web-Browser fuer VeloOS.\n"
"Einfach, schnell, direkt auf Bare-Metal ohne externe C++ Runtimes.\n\n"
"Schnellzugriff & Testseiten:\n"
"  -> http://neverssl.com\n"
"  -> http://info.cern.ch\n"
"  -> http://example.com\n"
"  -> C:/Users/Documents/README.TXT\n\n"
"Tippen Sie eine URL in die Adressleiste und druecken Sie Enter oder [Go].";

static inline int k_isspace(char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

void load_url(velo_window_t win, const char* input_url, int add_to_history);
void render_browser_ui(velo_window_t win);

// Einfaches Filtern von rohen HTML-Tags fuer saubere Textanzeige ohne Parser
static void strip_tags_copy(char *dest, const char *src, int max_len) {
    int dp = 0;
    int in_tag = 0;

    for (int i = 0; src[i] != '\0' && dp < max_len - 1; i++) {
        if (src[i] == '<') {
            in_tag = 1;
            continue;
        }
        if (src[i] == '>') {
            in_tag = 0;
            continue;
        }
        if (!in_tag) {
            if (src[i] == '\r') continue;
            dest[dp++] = src[i];
        }
    }
    dest[dp] = '\0';
}

static void create_tab(velo_window_t win, const char *url) {
    if (g_tab_count >= MAX_TABS) return;

    int new_idx = g_tab_count++;
    BrowserTab *t = &g_tabs[new_idx];
    memset(t, 0, sizeof(BrowserTab));
    t->active = 1;
    strncpy(t->title, "Neuer Tab", sizeof(t->title));

    g_active_tab = new_idx;
    load_url(win, url ? url : "about:home", 1);
}

static void close_tab(velo_window_t win, int tab_idx) {
    if (tab_idx < 0 || tab_idx >= g_tab_count) return;

    if (g_tab_count == 1) {
        load_url(win, "about:home", 1);
        return;
    }

    for (int i = tab_idx; i < g_tab_count - 1; i++) {
        g_tabs[i] = g_tabs[i + 1];
    }
    g_tab_count--;

    if (g_active_tab >= g_tab_count) {
        g_active_tab = g_tab_count - 1;
    }

    BrowserTab *cur = &g_tabs[g_active_tab];
    strncpy(g_url_input, cur->url, sizeof(g_url_input));
    g_url_cursor = (int)strlen(g_url_input);
}

void render_browser_ui(velo_window_t win) {
    g_browser_win = win;
    BrowserTab *cur_tab = &g_tabs[g_active_tab];

    // Hintergrund des Inhaltsbereichs
    velo_window_draw_rect_color(win, 0, 0, g_win_w, g_win_h, 0x000F172A);

    int content_y = TOP_CHROME_HEIGHT;
    int content_w = g_win_w;
    int content_h = g_win_h - TOP_CHROME_HEIGHT - STATUS_BAR_HEIGHT;

    // Dokumenten-Inhalt als formatierten Text darstellen
    if (cur_tab->content[0]) {
        int text_x = 24;
        int text_y = content_y + 16 - cur_tab->scroll_y;
        int max_chars = (content_w - 48) / 8;
        if (max_chars <= 0) max_chars = 1;

        const char *p = cur_tab->content;
        char line_buf[160];
        int lp = 0;
        int total_lines = 0;

        while (*p) {
            if (*p == '\n') {
                line_buf[lp] = '\0';
                if (text_y >= content_y && text_y < content_y + content_h - 16) {
                    velo_window_draw_text_colored(win, line_buf, text_x, text_y, 0x00E2E8F0);
                }
                text_y += 18;
                total_lines++;
                lp = 0;
            } else {
                if (lp < max_chars && lp < (int)sizeof(line_buf) - 1) {
                    line_buf[lp++] = *p;
                }
            }
            p++;
        }
        if (lp > 0) {
            line_buf[lp] = '\0';
            if (text_y >= content_y && text_y < content_y + content_h - 16) {
                velo_window_draw_text_colored(win, line_buf, text_x, text_y, 0x00E2E8F0);
            }
            total_lines++;
        }

        int doc_h = total_lines * 18 + 40;
        cur_tab->max_scroll_y = (doc_h > content_h) ? (doc_h - content_h) : 0;
    }

    // 1. Tab-Leiste
    velo_window_draw_gradient(win, 0, 0, g_win_w, TOP_TAB_HEIGHT, 0x00142A36, 0x000B1A24);
    velo_window_draw_rect_color(win, 0, TOP_TAB_HEIGHT - 1, g_win_w, 1, 0x003A7088);

    int max_tab_w = 180;
    int avail_w = g_win_w - 60;
    int tab_w = avail_w / (g_tab_count > 0 ? g_tab_count : 1);
    if (tab_w > max_tab_w) tab_w = max_tab_w;
    if (tab_w < 80) tab_w = 80;

    int cur_tab_x = 8;
    for (int i = 0; i < g_tab_count; i++) {
        BrowserTab *t = &g_tabs[i];
        int is_sel = (i == g_active_tab);

        UINT32 t_top = is_sel ? 0x002B5268 : 0x001B3644;
        UINT32 t_bot = is_sel ? 0x001B485A : 0x000E222E;
        UINT32 border = is_sel ? 0x005AC0E0 : 0x003A6878;

        velo_window_draw_gradient(win, cur_tab_x, 3, tab_w, 25, t_top, t_bot);
        velo_window_draw_rect_color(win, cur_tab_x, 3, tab_w, 1, border);
        velo_window_draw_rect_color(win, cur_tab_x, 3, 1, 25, border);
        velo_window_draw_rect_color(win, cur_tab_x + tab_w - 1, 3, 1, 25, border);

        char disp_title[24];
        strncpy(disp_title, t->title[0] ? t->title : t->url, 16);
        disp_title[16] = '\0';
        velo_window_draw_text_colored(win, disp_title, cur_tab_x + 8, 8, is_sel ? 0x00FFFFFF : 0x00CBD5E1);
        velo_window_draw_text_colored(win, "x", cur_tab_x + tab_w - 16, 8, is_sel ? 0x0094A3B8 : 0x0064748B);

        cur_tab_x += tab_w + 4;
    }

    if (g_tab_count < MAX_TABS) {
        velo_window_draw_gradient(win, cur_tab_x, 4, 22, 22, 0x001B485A, 0x000E222E);
        velo_window_draw_rect_color(win, cur_tab_x, 4, 22, 22, 0x003A7088);
        velo_window_draw_text_colored(win, "+", cur_tab_x + 7, 8, 0x00CBD5E1);
    }

    // 2. Toolbar & Adressleiste
    velo_window_draw_gradient(win, 0, TOP_TAB_HEIGHT, g_win_w, TOP_NAV_HEIGHT, 0x001B4D68, 0x000A2434);
    velo_window_draw_rect_color(win, 0, TOP_CHROME_HEIGHT - 1, g_win_w, 1, 0x003A7088);

    velo_ui_draw_nav_btn(win, 8, 34, "<", cur_tab->hist_pos > 0, 0);
    velo_ui_draw_nav_btn(win, 38, 34, ">", cur_tab->hist_pos < cur_tab->hist_count - 1, 0);
    velo_ui_draw_button(win, 68, 34, 28, 26, cur_tab->is_loading_async ? "X" : "~", 0, 0, 0, 0);
    velo_ui_draw_button(win, 98, 34, 28, 26, "H", 0, 0, 0, 0);

    int addr_x = 132;
    int go_btn_w = 48;
    int addr_w = g_win_w - addr_x - go_btn_w - 14;
    if (addr_w < 120) addr_w = 120;

    velo_ui_draw_searchbox(win, addr_x, 34, addr_w, g_url_input, g_url_cursor, g_url_focused);
    velo_ui_draw_button(win, addr_x + addr_w + 6, 34, go_btn_w, 26, "Go", 0, 0, 0, 0);

    // 3. Statusleiste
    int status_y = g_win_h - STATUS_BAR_HEIGHT;
    velo_window_draw_gradient(win, 0, status_y, g_win_w, STATUS_BAR_HEIGHT, 0x000E222E, 0x0006141C);
    velo_window_draw_rect_color(win, 0, status_y, g_win_w, 1, 0x002B5268);
    velo_window_draw_text_colored(win, cur_tab->status_text, 12, status_y + 4, 0x0094A3B8);

    const char *dns_status = cur_tab->is_loading_async ? "Lade Daten..." : "Online | Velo Native Engine";
    velo_window_draw_text_colored(win, dns_status, g_win_w - 240, status_y + 4, cur_tab->is_loading_async ? 0x0038BDF8 : 0x004ADE80);

    velo_window_redraw();
}

void load_url(velo_window_t win, const char* input_url, int add_to_history) {
    if (!input_url || !input_url[0]) return;

    BrowserTab *tab = &g_tabs[g_active_tab];

    char formatted_url[256];
    if (strcmp(input_url, "about:home") != 0 &&
        strncmp(input_url, "http://", 7) != 0 &&
        strncmp(input_url, "https://", 8) != 0 &&
        strncmp(input_url, "C:/", 3) != 0 &&
        strncmp(input_url, "c:/", 3) != 0 &&
        strncmp(input_url, "/", 1) != 0) {
        snprintf(formatted_url, sizeof(formatted_url), "http://%s", input_url);
    } else {
        strncpy(formatted_url, input_url, sizeof(formatted_url) - 1);
        formatted_url[sizeof(formatted_url) - 1] = '\0';
    }

    if (add_to_history && tab->hist_pos < 31) {
        tab->hist_pos++;
        strncpy(tab->history[tab->hist_pos], formatted_url, 255);
        tab->history[tab->hist_pos][255] = '\0';
        tab->hist_count = tab->hist_pos + 1;
    }

    strncpy(tab->url, formatted_url, sizeof(tab->url) - 1);
    strncpy(g_url_input, formatted_url, sizeof(g_url_input) - 1);
    g_url_cursor = (int)strlen(g_url_input);
    g_url_focused = 0;
    tab->scroll_y = 0;

    if (strcmp(formatted_url, "about:home") == 0) {
        strncpy(tab->content, g_default_home_text, sizeof(tab->content) - 1);
        strncpy(tab->title, "Startseite", sizeof(tab->title));
        snprintf(tab->status_text, sizeof(tab->status_text), "Startseite");
        tab->is_loading_async = 0;
        render_browser_ui(win);
        return;
    }

    if (strncmp(formatted_url, "http://", 7) == 0 || strncmp(formatted_url, "https://", 8) == 0) {
        char req_url[256];
        strncpy(req_url, formatted_url, sizeof(req_url) - 1);
        req_url[sizeof(req_url) - 1] = '\0';
        char *hash = strchr(req_url, '#');
        if (hash) *hash = '\0';

        strncpy(tab->title, formatted_url, sizeof(tab->title) - 1);
        snprintf(tab->status_text, sizeof(tab->status_text), "[1/4] DNS: Loese Host auf...");
        velo_http_async_start(req_url);
        tab->is_loading_async = 1;
        tab->timeout_timer = 0;
    } else {
        char raw[131072];
        int bytes = velo_read_file(formatted_url, raw, sizeof(raw) - 1);
        if (bytes <= 0) {
            snprintf(tab->content, sizeof(tab->content), "Datei nicht gefunden:\n%s", formatted_url);
            strncpy(tab->title, "Fehler", sizeof(tab->title));
        } else {
            raw[bytes] = '\0';
            strip_tags_copy(tab->content, raw, sizeof(tab->content));
            strncpy(tab->title, formatted_url, sizeof(tab->title));
        }
        snprintf(tab->status_text, sizeof(tab->status_text), "Fertig");
        tab->is_loading_async = 0;
    }

    render_browser_ui(win);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    velo_window_t win = velo_window_create("Velo Fox Web Browser", g_win_w, g_win_h);
    if (win < 0) return 0;
    g_browser_win = win;

    g_win_w = velo_window_get_width(win);
    g_win_h = velo_window_get_height(win);

    memset(g_tabs, 0, sizeof(g_tabs));
    g_tabs[0].active = 1;
    strncpy(g_tabs[0].title, "Startseite", sizeof(g_tabs[0].title));
    g_tab_count = 1;
    g_active_tab = 0;

    load_url(win, "about:home", 1);

    velo_event_t ev;
    while (1) {
        BrowserTab *cur = &g_tabs[g_active_tab];

        if (cur->is_loading_async) {
            int status = 0, bytes_data = 0;
            char raw_buf[131072];
            velo_http_async_poll(raw_buf, sizeof(raw_buf), &status, &bytes_data);

            int phase = (bytes_data >> 24) & 0xFF;
            int bytes = bytes_data & 0xFFFFFF;
            cur->timeout_timer++;

            if (status == HTTP_STATUS_READY) {
                cur->is_loading_async = 0;
                snprintf(cur->status_text, sizeof(cur->status_text), "Fertig (%d Bytes)", bytes);
                strip_tags_copy(cur->content, raw_buf, sizeof(cur->content));
                render_browser_ui(win);
            } else if (status == HTTP_STATUS_ERROR || cur->timeout_timer > 2000) {
                cur->is_loading_async = 0;
                snprintf(cur->content, sizeof(cur->content),
                    "Timeout / Verbindungsfehler\n"
                    "Die Verbindung zu %s konnte nicht hergestellt werden.\n", cur->url);
                strncpy(cur->title, "Fehler", sizeof(cur->title));
                snprintf(cur->status_text, sizeof(cur->status_text), "Timeout beim Laden");
                render_browser_ui(win);
            } else if (status == HTTP_STATUS_PENDING) {
                if (cur->timeout_timer % 30 == 0) {
                    if (phase == 0) snprintf(cur->status_text, sizeof(cur->status_text), "[1/4] DNS: Loese Host auf...");
                    else if (phase == 1) snprintf(cur->status_text, sizeof(cur->status_text), "[2/4] TCP: Verbinde (SYN)...");
                    else if (phase == 2) snprintf(cur->status_text, sizeof(cur->status_text), "[3/4] HTTP: Sende GET Request...");
                    else snprintf(cur->status_text, sizeof(cur->status_text), "[4/4] Empfange Daten (%d Bytes)...", bytes);
                    render_browser_ui(win);
                }
            }
        }

        int st = velo_poll_event(win, &ev);
        if (st == -1) break;

        if (st == 0) {
            velo_syscall(SYS_TASK_SLEEP, 1, 0, 0, 0);
            continue;
        }

        if (ev.type == VELO_EV_RESIZE) {
            g_win_w = ev.x;
            g_win_h = ev.y;
            render_browser_ui(win);
            continue;
        }

        if (ev.type == VELO_EV_SCROLL) {
            if (ev.y >= TOP_CHROME_HEIGHT && ev.y < g_win_h - STATUS_BAR_HEIGHT) {
                cur->scroll_y -= ev.scroll_y * 28;
                if (cur->scroll_y < 0) cur->scroll_y = 0;
                if (cur->scroll_y > cur->max_scroll_y) cur->scroll_y = cur->max_scroll_y;
                render_browser_ui(win);
            }
            continue;
        }

        if (ev.type == VELO_EV_CLICK) {
            if (ev.y < TOP_TAB_HEIGHT) {
                int max_tab_w = 180;
                int avail_w = g_win_w - 60;
                int tab_w = avail_w / (g_tab_count > 0 ? g_tab_count : 1);
                if (tab_w > max_tab_w) tab_w = max_tab_w;
                if (tab_w < 80) tab_w = 80;

                int cur_tab_x = 8;
                for (int i = 0; i < g_tab_count; i++) {
                    if (velo_ui_in_rect(ev.x, ev.y, cur_tab_x + tab_w - 20, 4, 18, 20)) {
                        close_tab(win, i);
                        render_browser_ui(win);
                        break;
                    }
                    else if (velo_ui_in_rect(ev.x, ev.y, cur_tab_x, 3, tab_w, 25)) {
                        g_active_tab = i;
                        BrowserTab *sel = &g_tabs[g_active_tab];
                        strncpy(g_url_input, sel->url, sizeof(g_url_input));
                        g_url_cursor = (int)strlen(g_url_input);
                        render_browser_ui(win);
                        break;
                    }
                    cur_tab_x += tab_w + 4;
                }

                if (g_tab_count < MAX_TABS && velo_ui_in_rect(ev.x, ev.y, cur_tab_x, 4, 22, 22)) {
                    create_tab(win, "about:home");
                    render_browser_ui(win);
                }
                continue;
            }

            int addr_x = 132, go_btn_w = 48;
            int addr_w = g_win_w - addr_x - go_btn_w - 14;
            if (addr_w < 120) addr_w = 120;

            if (velo_ui_in_rect(ev.x, ev.y, 8, 34, 26, 26)) {
                if (cur->hist_pos > 0) { cur->hist_pos--; load_url(win, cur->history[cur->hist_pos], 0); }
                continue;
            }
            if (velo_ui_in_rect(ev.x, ev.y, 38, 34, 26, 26)) {
                if (cur->hist_pos < cur->hist_count - 1) { cur->hist_pos++; load_url(win, cur->history[cur->hist_pos], 0); }
                continue;
            }
            if (velo_ui_in_rect(ev.x, ev.y, 68, 34, 28, 26)) {
                if (cur->is_loading_async) {
                    cur->is_loading_async = 0;
                    snprintf(cur->status_text, sizeof(cur->status_text), "Abgebrochen");
                    render_browser_ui(win);
                } else {
                    load_url(win, cur->url, 0);
                }
                continue;
            }
            if (velo_ui_in_rect(ev.x, ev.y, 98, 34, 28, 26)) { load_url(win, "about:home", 1); continue; }

            if (velo_ui_in_rect(ev.x, ev.y, addr_x, 34, addr_w, 26)) {
                g_url_focused = 1;
                int click_char = (ev.x - (addr_x + 8)) / 8;
                int ulen = (int)strlen(g_url_input);
                if (click_char < 0) click_char = 0;
                if (click_char > ulen) click_char = ulen;
                g_url_cursor = click_char;
                render_browser_ui(win);
                continue;
            }
            if (velo_ui_in_rect(ev.x, ev.y, addr_x + addr_w + 6, 34, go_btn_w, 26)) {
                load_url(win, g_url_input, 1);
                continue;
            }
        }

        if (ev.type == VELO_EV_KEY) {
            if (g_url_focused) {
                if (ev.key == '\n' || ev.key == '\r') {
                    load_url(win, g_url_input, 1);
                } else {
                    if (velo_ui_textbox_handle_key(g_url_input, sizeof(g_url_input), &g_url_cursor, ev.key)) {
                        render_browser_ui(win);
                    }
                }
            } else {
                if (ev.key == (char)0x82) { // Up
                    cur->scroll_y -= 28;
                    if (cur->scroll_y < 0) cur->scroll_y = 0;
                    render_browser_ui(win);
                } else if (ev.key == (char)0x83) { // Down
                    cur->scroll_y += 28;
                    if (cur->scroll_y > cur->max_scroll_y) cur->scroll_y = cur->max_scroll_y;
                    render_browser_ui(win);
                }
            }
        }
    }

    return 0;
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <velo/window.h>
#include <velo/syscall.h>
#include <velo/litehtml_bridge.h>

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
    int            active;
    char           url[256];
    char           title[64];
    char           html[131072];
    litehtml_doc_t doc;
    char           history[32][256];
    int            hist_count;
    int            hist_pos;
    int            scroll_y;
    int            max_scroll_y;
    int            is_loading_async;
    int            timeout_timer;
    char           status_text[64];
} BrowserTab;

static BrowserTab g_tabs[MAX_TABS];
static int g_active_tab = 0;
static int g_tab_count = 1;

static const char* g_default_home_html = 
"<!DOCTYPE html><html><head><title>Startseite</title>"
"<style>body { font-family: sans-serif; color: #1e293b; padding: 15px; }"
"h1 { color: #0f172a; border-bottom: 2px solid #38bdf8; padding-bottom: 8px; }"
"h2 { color: #1e3a8a; margin-top: 20px; }"
"a { color: #2563eb; text-decoration: underline; }"
"li { margin-bottom: 8px; }"
"hr { border: 0; border-top: 1px solid #cbd5e1; margin: 20px 0; }"
"</style></head><body>"
"<h1>Velo Fox Web Browser</h1>"
"<p>Willkommen in Ihrem nativen 64-Bit Web-Browser fuer <b>VeloOS</b> (Powered by <i>litehtml</i>).</p>"
"<hr>"
"<h2>Webseiten & Schnellzugriff</h2>"
"<ul>"
"<li><a href=\"http://neverssl.com\">http://neverssl.com</a> - Schnelle unverschluesselte HTTP-Testseite</li>"
"<li><a href=\"http://info.cern.ch/hypertext/WWW/TheProject.html\">http://info.cern.ch</a> - Erste Webseite der Welt</li>"
"<li><a href=\"http://example.com\">http://example.com</a> - IANA RFC Testseite</li>"
"<li><a href=\"C:/Users/Documents/README.TXT\">C:/Users/Documents/README.TXT</a> - Lokale Datei</li>"
"</ul>"
"<hr>"
"<p>Vollstaendige CSS/HTML-Layout-Unterstuetzung ueber LiteHTML-Bridge.</p>"
"</body></html>";

static inline int k_isspace(char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

void load_url(velo_window_t win, const char* input_url, int add_to_history);
void render_browser_ui(velo_window_t win);

// =========================================================================
// LITEHTML BRIDGE CALLBACKS
// =========================================================================
static void on_bridge_draw_text(const char *text, int x, int y, unsigned int color) {
    if (g_browser_win >= 0 && text) {
        velo_window_draw_text_colored(g_browser_win, text, x, y, (UINT32)color);
    }
}

static void on_bridge_draw_rect(int x, int y, int w, int h, unsigned int color) {
    if (g_browser_win >= 0 && w > 0 && h > 0) {
        velo_window_draw_rect_color(g_browser_win, x, y, w, h, (UINT32)color);
    }
}

static void resolve_relative_url(char *out, const char *base_url, const char *link_target, int max_len) {
    if (!link_target || !link_target[0]) {
        out[0] = '\0';
        return;
    }

    if (strncmp(link_target, "http://", 7) == 0 ||
        strncmp(link_target, "https://", 8) == 0 ||
        strcmp(link_target, "about:home") == 0 ||
        strncmp(link_target, "C:/", 3) == 0 ||
        strncmp(link_target, "c:/", 3) == 0) {
        strncpy(out, link_target, max_len - 1);
        out[max_len - 1] = '\0';
        return;
    }

    if (!base_url || strncmp(base_url, "http", 4) != 0) {
        snprintf(out, max_len, "http://%s", link_target);
        return;
    }

    char base_no_hash[256];
    strncpy(base_no_hash, base_url, sizeof(base_no_hash));
    char *h = strchr(base_no_hash, '#');
    if (h) *h = '\0';

    const char *scheme_end = strstr(base_no_hash, "://");
    if (!scheme_end) {
        snprintf(out, max_len, "http://%s", link_target);
        return;
    }
    scheme_end += 3;

    const char *first_slash = strchr(scheme_end, '/');
    if (link_target[0] == '/') {
        int host_len = first_slash ? (int)(first_slash - base_no_hash) : (int)strlen(base_no_hash);
        char host_part[256];
        if (host_len > 255) host_len = 255;
        strncpy(host_part, base_no_hash, host_len);
        host_part[host_len] = '\0';
        snprintf(out, max_len, "%s%s", host_part, link_target);
        return;
    }

    const char *last_slash = strrchr(base_no_hash, '/');
    if (!last_slash || last_slash < scheme_end) {
        snprintf(out, max_len, "%s/%s", base_no_hash, link_target);
    } else {
        int dir_len = (int)(last_slash - base_no_hash) + 1;
        char dir_part[256];
        if (dir_len > 255) dir_len = 255;
        strncpy(dir_part, base_no_hash, dir_len);
        dir_part[dir_len] = '\0';
        snprintf(out, max_len, "%s%s", dir_part, link_target);
    }
}

static void on_bridge_link_click(const char *url) {
    if (!url || !url[0]) return;
    BrowserTab *tab = &g_tabs[g_active_tab];
    char resolved[256];
    resolve_relative_url(resolved, tab->url, url, sizeof(resolved));
    load_url(g_browser_win, resolved, 1);
}

static void extract_html_title(const char *html, char *out_title, int max_len) {
    if (!html || !out_title) return;
    const char *p = html;
    while (*p) {
        if (*p == '<' && (strncasecmp(p + 1, "title>", 6) == 0)) {
            p += 7;
            while (*p && k_isspace(*p)) p++;
            int t = 0;
            while (*p && *p != '<' && t < max_len - 1) {
                out_title[t++] = *p++;
            }
            while (t > 0 && k_isspace(out_title[t - 1])) t--;
            out_title[t] = '\0';
            return;
        }
        p++;
    }
}

static void tab_rebuild_document(BrowserTab *tab, int content_w, int content_h) {
    if (!tab) return;
    if (tab->doc) {
        velo_litehtml_destroy_document(tab->doc);
        tab->doc = NULL;
    }

    if (tab->html[0]) {
        tab->doc = velo_litehtml_create_document(tab->html);
        if (tab->doc) {
            int render_w = content_w - 40;
            if (render_w < 200) render_w = 200;
            velo_litehtml_render(tab->doc, render_w);

            int doc_h = velo_litehtml_get_height(tab->doc);
            tab->max_scroll_y = (doc_h > content_h) ? (doc_h - content_h + 40) : 0;
            if (tab->scroll_y > tab->max_scroll_y) tab->scroll_y = tab->max_scroll_y;
            if (tab->scroll_y < 0) tab->scroll_y = 0;
        }
    }
}

static void create_tab(velo_window_t win, const char *url) {
    if (g_tab_count >= MAX_TABS) return;

    int new_idx = g_tab_count++;
    BrowserTab *t = &g_tabs[new_idx];
    memset(t, 0, sizeof(BrowserTab));
    t->active = 1;
    t->doc = NULL;
    strncpy(t->title, "Neuer Tab", sizeof(t->title));

    g_active_tab = new_idx;
    load_url(win, url ? url : "about:home", 1);
}

static void close_tab(velo_window_t win, int tab_idx) {
    if (tab_idx < 0 || tab_idx >= g_tab_count) return;

    if (g_tabs[tab_idx].doc) {
        velo_litehtml_destroy_document(g_tabs[tab_idx].doc);
        g_tabs[tab_idx].doc = NULL;
    }

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

    velo_window_draw_rect_color(win, 0, 0, g_win_w, g_win_h, 0x00FFFFFF);

    int content_y = TOP_CHROME_HEIGHT;
    int content_w = g_win_w;
    int content_h = g_win_h - TOP_CHROME_HEIGHT - STATUS_BAR_HEIGHT;

    // LiteHTML Dokument zeichnen
    if (cur_tab->doc) {
        int render_w = content_w - 40;
        if (render_w < 200) render_w = 200;
        velo_litehtml_render(cur_tab->doc, render_w);

        int doc_h = velo_litehtml_get_height(cur_tab->doc);
        cur_tab->max_scroll_y = (doc_h > content_h) ? (doc_h - content_h + 40) : 0;
        if (cur_tab->scroll_y > cur_tab->max_scroll_y) cur_tab->scroll_y = cur_tab->max_scroll_y;
        if (cur_tab->scroll_y < 0) cur_tab->scroll_y = 0;

        velo_litehtml_draw(cur_tab->doc,
                           20, content_y + 16 - cur_tab->scroll_y,
                           0, content_y,
                           content_w, content_h);
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

    // 2. Toolbar
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

    const char *dns_status = cur_tab->is_loading_async ? "Lade Daten..." : "Online | litehtml Engine";
    velo_window_draw_text_colored(win, dns_status, g_win_w - 210, status_y + 4, cur_tab->is_loading_async ? 0x0038BDF8 : 0x004ADE80);

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

    int content_w = g_win_w;
    int content_h = g_win_h - TOP_CHROME_HEIGHT - STATUS_BAR_HEIGHT;

    if (strcmp(formatted_url, "about:home") == 0) {
        strncpy(tab->html, g_default_home_html, sizeof(tab->html) - 1);
        strncpy(tab->title, "Startseite", sizeof(tab->title));
        snprintf(tab->status_text, sizeof(tab->status_text), "Startseite");
        tab->is_loading_async = 0;
        tab_rebuild_document(tab, content_w, content_h);
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
        int bytes = velo_read_file(formatted_url, tab->html, sizeof(tab->html) - 1);
        if (bytes <= 0) {
            snprintf(tab->html, sizeof(tab->html), "<h1>Datei nicht gefunden</h1><p>%s</p>", formatted_url);
            strncpy(tab->title, "Fehler", sizeof(tab->title));
        } else {
            tab->html[bytes] = '\0';
            extract_html_title(tab->html, tab->title, sizeof(tab->title));
            if (!tab->title[0]) strncpy(tab->title, formatted_url, sizeof(tab->title));
        }
        snprintf(tab->status_text, sizeof(tab->status_text), "Fertig");
        tab->is_loading_async = 0;
        tab_rebuild_document(tab, content_w, content_h);
    }

    render_browser_ui(win);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    velo_litehtml_init(on_bridge_draw_text, on_bridge_draw_rect, on_bridge_link_click);

    velo_window_t win = velo_window_create("Velo Fox Web Browser", g_win_w, g_win_h);
    if (win < 0) return 0;
    g_browser_win = win;

    g_win_w = velo_window_get_width(win);
    g_win_h = velo_window_get_height(win);

    memset(g_tabs, 0, sizeof(g_tabs));
    g_tabs[0].active = 1;
    g_tabs[0].doc = NULL;
    strncpy(g_tabs[0].title, "Startseite", sizeof(g_tabs[0].title));
    g_tab_count = 1;
    g_active_tab = 0;

    load_url(win, "about:home", 1);

    velo_event_t ev;
    while (1) {
        BrowserTab *cur = &g_tabs[g_active_tab];

        if (cur->is_loading_async) {
            int status = 0, bytes_data = 0;
            velo_http_async_poll(cur->html, sizeof(cur->html), &status, &bytes_data);

            int phase = (bytes_data >> 24) & 0xFF;
            int bytes = bytes_data & 0xFFFFFF;
            cur->timeout_timer++;

            if (status == HTTP_STATUS_READY) {
                cur->is_loading_async = 0;
                snprintf(cur->status_text, sizeof(cur->status_text), "Fertig (%d Bytes)", bytes);
                extract_html_title(cur->html, cur->title, sizeof(cur->title));
                if (!cur->title[0]) strncpy(cur->title, cur->url, sizeof(cur->title));

                int content_w = g_win_w;
                int content_h = g_win_h - TOP_CHROME_HEIGHT - STATUS_BAR_HEIGHT;
                tab_rebuild_document(cur, content_w, content_h);

                render_browser_ui(win);
            } else if (status == HTTP_STATUS_ERROR || cur->timeout_timer > 2000) {
                cur->is_loading_async = 0;
                snprintf(cur->html, sizeof(cur->html),
                    "<h1>Timeout / Verbindungsfehler</h1>"
                    "<p>Die Verbindung zu <b>%s</b> konnte nicht hergestellt werden.</p>"
                    "<hr><p><a href=\"about:home\">Zurueck zur Startseite</a></p>", cur->url);
                strncpy(cur->title, "Fehler", sizeof(cur->title));
                snprintf(cur->status_text, sizeof(cur->status_text), "Timeout beim Laden");

                int content_w = g_win_w;
                int content_h = g_win_h - TOP_CHROME_HEIGHT - STATUS_BAR_HEIGHT;
                tab_rebuild_document(cur, content_w, content_h);

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
            int content_w = g_win_w;
            int content_h = g_win_h - TOP_CHROME_HEIGHT - STATUS_BAR_HEIGHT;
            tab_rebuild_document(cur, content_w, content_h);
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

            // Klick in LiteHTML-Dokumentenflaeche
            if (ev.y >= TOP_CHROME_HEIGHT && ev.y < g_win_h - STATUS_BAR_HEIGHT) {
                if (cur->doc) {
                    int doc_x = ev.x - 20;
                    int doc_y = ev.y - (TOP_CHROME_HEIGHT + 16 - cur->scroll_y);
                    velo_litehtml_mouse_click(cur->doc, doc_x, doc_y, ev.x, ev.y);
                }
                if (g_url_focused) {
                    g_url_focused = 0;
                    render_browser_ui(win);
                }
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
                if (ev.key == (char)0x82) {
                    cur->scroll_y -= 28;
                    if (cur->scroll_y < 0) cur->scroll_y = 0;
                    render_browser_ui(win);
                } else if (ev.key == (char)0x83) {
                    cur->scroll_y += 28;
                    if (cur->scroll_y > cur->max_scroll_y) cur->scroll_y = cur->max_scroll_y;
                    render_browser_ui(win);
                }
            }
        }
    }

    return 0;
}
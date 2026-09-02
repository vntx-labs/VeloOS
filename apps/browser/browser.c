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
#define MAX_LINKS 128
#define MAX_ANCHORS 128

extern int velo_ui_textbox_handle_key(char *text, int max_len, int *cursor_pos, char key);

static int g_win_w = WIN_DEFAULT_W;
static int g_win_h = WIN_DEFAULT_H;

// Adressleiste & Navigation
static char g_url_input[256] = "about:home";
static int  g_url_cursor = 10;
static int  g_url_focused = 0;

typedef struct {
    int x, y, w, h;
    char target[256];
} ClickableLink;

static ClickableLink g_links[MAX_LINKS];
static int g_link_count = 0;

// Anker-Tabelle für Seiten-Sprünge (#anchor)
typedef struct {
    char name[64];
    int y_pos;
} PageAnchor;

static PageAnchor g_anchors[MAX_ANCHORS];
static int g_anchor_count = 0;

typedef struct {
    int  active;
    char url[256];
    char title[64];
    char html[131072];
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

static const char* g_default_home_html = 
"<h1>Velo Fox Web Browser</h1>"
"<p>Willkommen in Ihrem nativen 64-Bit Web-Browser fuer <b>VeloOS</b>.</p>"
"<hr>"
"<h2>Webseiten & Schnellzugriff</h2>"
"<ul>"
"<li><a href=\"http://neverssl.com\">http://neverssl.com</a> - Schnelle unverschluesselte HTTP-Testseite</li>"
"<li><a href=\"http://info.cern.ch/hypertext/WWW/TheProject.html\">http://info.cern.ch</a> - Die erste Webseite der Welt</li>"
"<li><a href=\"http://example.com\">http://example.com</a> - IANA RFC Testseite</li>"
"<li><a href=\"C:/Users/Documents/README.TXT\">C:/Users/Documents/README.TXT</a> - Lokale Datei</li>"
"</ul>"
"<hr>"
"<p>Unterstuetzt historische CERN HTML 1.0/2.0 Dokumente, Definitionslisten und Seiten-Anker.</p>";

static inline int k_isspace(char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

static UINT32 parse_color_str(const char *str, UINT32 def_col) {
    if (!str || !str[0]) return def_col;
    while (*str == ' ' || *str == '"' || *str == '\'') str++;

    if (*str == '#') {
        str++;
        unsigned int hex = 0;
        for (int i = 0; i < 6 && str[i]; i++) {
            char c = str[i];
            int v = 0;
            if (c >= '0' && c <= '9') v = c - '0';
            else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
            hex = (hex << 4) | v;
        }
        return hex;
    }

    if (strncmp(str, "red", 3) == 0) return 0x00DC2626;
    if (strncmp(str, "green", 5) == 0) return 0x0016A34A;
    if (strncmp(str, "blue", 4) == 0) return 0x002563EB;
    if (strncmp(str, "black", 5) == 0) return 0x000F172A;
    if (strncmp(str, "gray", 4) == 0 || strncmp(str, "grey", 4) == 0) return 0x0064748B;
    if (strncmp(str, "orange", 6) == 0) return 0x00EA580C;
    if (strncmp(str, "yellow", 6) == 0) return 0x00CA8A04;
    if (strncmp(str, "purple", 6) == 0) return 0x009333EA;

    return def_col;
}

static void resolve_relative_url(char *out, const char *base_url, const char *link_target, int max_len) {
    if (!link_target || !link_target[0]) {
        out[0] = '\0';
        return;
    }

    if (link_target[0] == '#') {
        char base_clean[256];
        strncpy(base_clean, base_url ? base_url : "", sizeof(base_clean));
        char *h = strchr(base_clean, '#');
        if (h) *h = '\0';
        snprintf(out, max_len, "%s%s", base_clean, link_target);
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

static void unescape_html_entities(char *dest, const char *src, int max_len) {
    int d = 0;
    while (*src && d < max_len - 1) {
        if (*src == '&') {
            if (strncmp(src, "&amp;", 5) == 0) { dest[d++] = '&'; src += 5; continue; }
            if (strncmp(src, "&lt;", 4) == 0) { dest[d++] = '<'; src += 4; continue; }
            if (strncmp(src, "&gt;", 4) == 0) { dest[d++] = '>'; src += 4; continue; }
            if (strncmp(src, "&quot;", 6) == 0) { dest[d++] = '"'; src += 6; continue; }
            if (strncmp(src, "&apos;", 6) == 0) { dest[d++] = '\''; src += 6; continue; }
            if (strncmp(src, "&nbsp;", 6) == 0) { dest[d++] = ' '; src += 6; continue; }
            if (strncmp(src, "&copy;", 6) == 0) { dest[d++] = '(c)'; src += 6; continue; }
            if (strncmp(src, "&bull;", 6) == 0) { dest[d++] = '*'; src += 6; continue; }
            if (strncmp(src, "&mdash;", 7) == 0) { dest[d++] = '-'; src += 7; continue; }
            if (strncmp(src, "&ndash;", 7) == 0) { dest[d++] = '-'; src += 7; continue; }
        }
        dest[d++] = *src++;
    }
    dest[d] = '\0';
}

static void extract_html_title(const char *html, char *out_title, int max_len) {
    if (!html || !out_title) return;
    const char *p = html;
    while (*p) {
        if (*p == '<' && (strncmp(p + 1, "title>", 6) == 0 || strncmp(p + 1, "TITLE>", 6) == 0)) {
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

// ==========================================
// VOLLSTÄNDIGE HTML 1.0 / 2.0 / 5 ENGINE
// ==========================================
static void render_html_content(velo_window_t win, BrowserTab *tab, int content_y, int content_w, int content_h) {
    if (!tab || !tab->html[0]) return;

    g_link_count = 0;
    g_anchor_count = 0;

    int cur_x = 20;
    int cur_y = content_y + 16 - tab->scroll_y;
    int max_x = content_w - 30;
    int line_h = 18;
    int indent_x = 20;

    UINT32 cur_color = 0x001E293B;
    int is_bold = 0;
    int is_underline = 0;
    int is_strike = 0;
    int is_mark = 0;
    int is_pre = 0;
    int is_blockquote = 0;
    int list_order_num = 0;
    int is_ordered_list = 0;
    
    int in_table = 0;
    int table_col_idx = 0;
    int table_col_w = (content_w - 60) / 4;
    if (table_col_w < 80) table_col_w = 80;

    int is_h1 = 0, is_h2 = 0, is_h3 = 0;
    int in_link = 0;
    char current_link_target[256] = "";

    int had_space = 0;
    int at_line_start = 1;

    const char *p = tab->html;
    int guard = 0;

    while (*p && ++guard < 100000) {
        if (is_pre) {
            if (strncmp(p, "</pre>", 6) == 0 || strncmp(p, "</PRE>", 6) == 0) {
                is_pre = 0; p += 6; cur_x = indent_x; cur_y += 16; at_line_start = 1; had_space = 0;
                continue;
            }
            if (*p == '\n') {
                cur_x = (is_blockquote ? 44 : 28); cur_y += 16; p++;
                continue;
            }
            char ch = *p++;
            if (cur_y >= content_y - 16 && cur_y < content_y + content_h) {
                char s[2] = {ch, '\0'};
                velo_window_draw_text_colored(win, s, cur_x, cur_y, 0x000F172A);
            }
            cur_x += 8;
            continue;
        }

        if (*p == '<') {
            p++;

            // Unsichtbare Tags, Title & NEXTID überspringen
            if (strncmp(p, "script", 6) == 0 || strncmp(p, "SCRIPT", 6) == 0 ||
                strncmp(p, "style", 5) == 0 || strncmp(p, "STYLE", 5) == 0 ||
                strncmp(p, "head", 4) == 0 || strncmp(p, "HEAD", 4) == 0 ||
                strncmp(p, "title", 5) == 0 || strncmp(p, "TITLE", 5) == 0 ||
                strncmp(p, "NEXTID", 6) == 0 || strncmp(p, "nextid", 6) == 0 ||
                strncmp(p, "svg", 3) == 0 || strncmp(p, "SVG", 3) == 0) {
                while (*p && *p != '>') p++;
                if (*p == '>') p++;
                while (*p && !(p[0] == '<' && p[1] == '/')) p++;
                while (*p && *p != '>') p++;
                if (*p == '>') p++;
                continue;
            }

            char tag[32];
            int t_len = 0;
            while (*p && *p != '>' && !k_isspace(*p) && t_len < 31) {
                char c = *p++;
                if (c >= 'A' && c <= 'Z') c += 32;
                tag[t_len++] = c;
            }
            tag[t_len] = '\0';

            int has_href = 0;
            current_link_target[0] = '\0';

            // HTML 1.0 / 2.0 Attribut-Parser (Unterstützt Quoted & Unquoted)
            while (*p && *p != '>') {
                while (*p && k_isspace(*p)) p++;

                if (strcmp(tag, "a") == 0) {
                    if (strncasecmp(p, "href=", 5) == 0) {
                        p += 5;
                        char quote = (*p == '"' || *p == '\'') ? *p++ : 0;
                        int l = 0;
                        while (*p && (quote ? (*p != quote) : (!k_isspace(*p) && *p != '>')) && l < 255) {
                            current_link_target[l++] = *p++;
                        }
                        current_link_target[l] = '\0';
                        if (quote && *p == quote) p++;
                        has_href = 1;
                        continue;
                    }
                    else if (strncasecmp(p, "name=", 5) == 0) {
                        p += 5;
                        char quote = (*p == '"' || *p == '\'') ? *p++ : 0;
                        char anchor_name[64]; int l = 0;
                        while (*p && (quote ? (*p != quote) : (!k_isspace(*p) && *p != '>')) && l < 63) {
                            anchor_name[l++] = *p++;
                        }
                        anchor_name[l] = '\0';
                        if (quote && *p == quote) p++;

                        // Anker-Position registrieren für Sprungmarken (#name)
                        if (g_anchor_count < MAX_ANCHORS && anchor_name[0]) {
                            strncpy(g_anchors[g_anchor_count].name, anchor_name, 63);
                            g_anchors[g_anchor_count].y_pos = cur_y + tab->scroll_y - content_y;
                            g_anchor_count++;
                        }
                        continue;
                    }
                } else if (strncasecmp(p, "color=", 6) == 0) {
                    p += 6;
                    char quote = (*p == '"' || *p == '\'') ? *p++ : 0;
                    char col_buf[32]; int cl = 0;
                    while (*p && (quote ? (*p != quote) : (!k_isspace(*p) && *p != '>')) && cl < 31) {
                        col_buf[cl++] = *p++;
                    }
                    col_buf[cl] = '\0';
                    cur_color = parse_color_str(col_buf, cur_color);
                    if (quote && *p == quote) p++;
                    continue;
                }
                p++;
            }

            if (*p == '>') p++;

            // ==========================================
            // TAG REGELN (Inklusive <DL>, <DT>, <DD>)
            // ==========================================
            if (strcmp(tag, "h1") == 0) {
                cur_x = indent_x; cur_y += 26; is_h1 = 1; is_bold = 1; cur_color = 0x000F172A; at_line_start = 1; had_space = 0;
            } else if (strcmp(tag, "/h1") == 0) {
                cur_x = indent_x; cur_y += 24; is_h1 = 0; is_bold = 0; cur_color = 0x001E293B; at_line_start = 1; had_space = 0;
            } else if (strcmp(tag, "h2") == 0) {
                cur_x = indent_x; cur_y += 20; is_h2 = 1; is_bold = 1; cur_color = 0x001E3A8A; at_line_start = 1; had_space = 0;
            } else if (strcmp(tag, "/h2") == 0) {
                cur_x = indent_x; cur_y += 20; is_h2 = 0; is_bold = 0; cur_color = 0x001E293B; at_line_start = 1; had_space = 0;
            } else if (strcmp(tag, "h3") == 0 || strcmp(tag, "h4") == 0) {
                cur_x = indent_x; cur_y += 16; is_h3 = 1; is_bold = 1; cur_color = 0x00334155; at_line_start = 1; had_space = 0;
            } else if (strcmp(tag, "/h3") == 0 || strcmp(tag, "/h4") == 0) {
                cur_x = indent_x; cur_y += 16; is_h3 = 0; is_bold = 0; cur_color = 0x001E293B; at_line_start = 1; had_space = 0;
            } else if (strcmp(tag, "p") == 0) {
                cur_x = indent_x; cur_y += 16; at_line_start = 1; had_space = 0;
            } else if (strcmp(tag, "/p") == 0) {
                cur_x = indent_x; cur_y += 12; at_line_start = 1; had_space = 0;
            } else if (strcmp(tag, "div") == 0 || strcmp(tag, "section") == 0 || strcmp(tag, "article") == 0) {
                cur_x = indent_x; cur_y += 12; at_line_start = 1; had_space = 0;
            } else if (strcmp(tag, "/div") == 0 || strcmp(tag, "/section") == 0) {
                cur_x = indent_x; cur_y += 10; at_line_start = 1; had_space = 0;
            } else if (strcmp(tag, "br") == 0) {
                cur_x = indent_x; cur_y += line_h; at_line_start = 1; had_space = 0;
            } else if (strcmp(tag, "blockquote") == 0) {
                is_blockquote = 1; indent_x = 44; cur_x = 44; cur_y += 14; at_line_start = 1; had_space = 0;
                if (cur_y >= content_y - 16 && cur_y < content_y + content_h) {
                    velo_window_draw_rect_color(win, 24, cur_y - 2, 4, 24, 0x003B82F6);
                }
            } else if (strcmp(tag, "/blockquote") == 0) {
                is_blockquote = 0; indent_x = 20; cur_x = 20; cur_y += 16; at_line_start = 1; had_space = 0;
            } 
            // DEFINITION LISTS (<DL>, <DT>, <DD>)
            else if (strcmp(tag, "dl") == 0) {
                indent_x = 20; cur_x = 20; cur_y += 14; at_line_start = 1; had_space = 0;
            } else if (strcmp(tag, "/dl") == 0) {
                indent_x = 20; cur_x = 20; cur_y += 16; at_line_start = 1; had_space = 0;
            } else if (strcmp(tag, "dt") == 0) {
                indent_x = 20; cur_x = 20; cur_y += 18; is_bold = 1; cur_color = 0x000F172A; at_line_start = 1; had_space = 0;
            } else if (strcmp(tag, "/dt") == 0) {
                is_bold = 0; cur_color = 0x001E293B;
            } else if (strcmp(tag, "dd") == 0) {
                indent_x = 44; cur_x = 44; cur_y += 16; is_bold = 0; cur_color = 0x001E293B; at_line_start = 1; had_space = 0;
            } else if (strcmp(tag, "/dd") == 0) {
                indent_x = 20; cur_x = 20; cur_y += 10; at_line_start = 1; had_space = 0;
            }
            else if (strcmp(tag, "pre") == 0 || strcmp(tag, "code") == 0) {
                is_pre = 1; cur_x = (is_blockquote ? 44 : 28); cur_y += 16; at_line_start = 1; had_space = 0;
                if (cur_y >= content_y - 16 && cur_y < content_y + content_h) {
                    velo_window_draw_rect_color(win, cur_x - 6, cur_y - 4, max_x - cur_x + 10, 48, 0x00F1F5F9);
                    velo_window_draw_rect_color(win, cur_x - 6, cur_y - 4, max_x - cur_x + 10, 1, 0x00CBD5E1);
                }
            } else if (strcmp(tag, "ul") == 0) {
                is_ordered_list = 0; cur_x = 20; cur_y += 10; at_line_start = 1; had_space = 0;
            } else if (strcmp(tag, "/ul") == 0 || strcmp(tag, "/ol") == 0) {
                cur_x = 20; cur_y += 12; is_ordered_list = 0; list_order_num = 0; at_line_start = 1; had_space = 0;
            } else if (strcmp(tag, "ol") == 0) {
                is_ordered_list = 1; list_order_num = 1; cur_x = 20; cur_y += 10; at_line_start = 1; had_space = 0;
            } else if (strcmp(tag, "li") == 0) {
                cur_x = 24; cur_y += 18; at_line_start = 1; had_space = 0;
                if (cur_y >= content_y - 16 && cur_y < content_y + content_h) {
                    if (is_ordered_list) {
                        char num_str[16]; snprintf(num_str, sizeof(num_str), "%d.", list_order_num++);
                        velo_window_draw_text_colored(win, num_str, cur_x, cur_y, 0x002563EB);
                        cur_x += (int)strlen(num_str) * 8 + 6;
                    } else {
                        velo_window_draw_text_colored(win, "*", cur_x, cur_y, 0x002563EB);
                        cur_x += 16;
                    }
                }
            } else if (strcmp(tag, "hr") == 0) {
                cur_x = indent_x; cur_y += 10;
                if (cur_y >= content_y && cur_y < content_y + content_h) {
                    velo_window_draw_rect_color(win, 20, cur_y, max_x - 20, 1, 0x00CBD5E1);
                }
                cur_y += 14; at_line_start = 1; had_space = 0;
            } else if (strcmp(tag, "b") == 0 || strcmp(tag, "strong") == 0) {
                is_bold = 1;
            } else if (strcmp(tag, "/b") == 0 || strcmp(tag, "/strong") == 0) {
                is_bold = 0;
            } else if (strcmp(tag, "i") == 0 || strcmp(tag, "em") == 0) {
                cur_color = 0x00334155;
            } else if (strcmp(tag, "/i") == 0 || strcmp(tag, "/em") == 0) {
                cur_color = 0x001E293B;
            } else if (strcmp(tag, "u") == 0 || strcmp(tag, "ins") == 0) {
                is_underline = 1;
            } else if (strcmp(tag, "/u") == 0 || strcmp(tag, "/ins") == 0) {
                is_underline = 0;
            } else if (strcmp(tag, "s") == 0 || strcmp(tag, "strike") == 0 || strcmp(tag, "del") == 0) {
                is_strike = 1;
            } else if (strcmp(tag, "/s") == 0 || strcmp(tag, "/strike") == 0 || strcmp(tag, "/del") == 0) {
                is_strike = 0;
            } else if (strcmp(tag, "mark") == 0) {
                is_mark = 1;
            } else if (strcmp(tag, "/mark") == 0) {
                is_mark = 0;
            } else if (strcmp(tag, "a") == 0) {
                if (has_href) {
                    in_link = 1;
                    cur_color = 0x002563EB;
                } else {
                    in_link = 0; // Nur ein Ankerziel, kein Link
                }
            } else if (strcmp(tag, "/a") == 0) {
                in_link = 0;
                cur_color = 0x001E293B;
            }
            continue;
        }

        if (k_isspace(*p)) {
            int newlines = 0;
            while (*p && k_isspace(*p)) {
                if (*p == '\n') newlines++;
                p++;
            }
            if (newlines >= 2) {
                cur_x = indent_x;
                cur_y += line_h;
                at_line_start = 1;
                had_space = 0;
            } else {
                had_space = 1;
            }
            continue;
        }

        char raw_word[128], clean_word[128];
        int w_len = 0;
        while (*p && !k_isspace(*p) && *p != '<' && w_len < 120) {
            raw_word[w_len++] = *p++;
        }
        raw_word[w_len] = '\0';

        unescape_html_entities(clean_word, raw_word, sizeof(clean_word));
        int render_len = (int)strlen(clean_word);

        if (render_len > 0) {
            int space_px = (had_space && !at_line_start) ? 8 : 0;
            int word_px = render_len * 8;

            if (cur_x + space_px + word_px > max_x) {
                cur_x = indent_x;
                cur_y += (is_h1 ? 24 : (is_h2 ? 20 : (is_h3 ? 18 : line_h)));
                space_px = 0;
                at_line_start = 1;
            }

            if (in_link && space_px > 0 && cur_y >= content_y - 16 && cur_y < content_y + content_h) {
                velo_window_draw_rect_color(win, cur_x, cur_y + 15, space_px, 1, 0x002563EB);
            }

            cur_x += space_px;

            if (cur_y >= content_y - 16 && cur_y < content_y + content_h) {
                if (is_mark) {
                    velo_window_draw_rect_color(win, cur_x, cur_y - 1, word_px, 17, 0x00FEF08A);
                }

                velo_window_draw_text_colored(win, clean_word, cur_x, cur_y, cur_color);
                
                if (is_bold || is_h1 || is_h2) {
                    velo_window_draw_text_colored(win, clean_word, cur_x + 1, cur_y, cur_color);
                }

                if (is_strike) {
                    velo_window_draw_rect_color(win, cur_x, cur_y + 8, word_px, 1, 0x00EF4444);
                }

                if (is_underline && !in_link) {
                    velo_window_draw_rect_color(win, cur_x, cur_y + 15, word_px, 1, cur_color);
                }

                if (in_link) {
                    velo_window_draw_rect_color(win, cur_x, cur_y + 15, word_px, 1, 0x002563EB);
                    if (g_link_count < MAX_LINKS) {
                        int total_link_w = space_px + word_px;
                        g_links[g_link_count].x = cur_x - space_px;
                        g_links[g_link_count].y = cur_y;
                        g_links[g_link_count].w = total_link_w;
                        g_links[g_link_count].h = 16;
                        strncpy(g_links[g_link_count].target, current_link_target, 255);
                        g_link_count++;
                    }
                }
            }

            cur_x += word_px;
            at_line_start = 0;
            had_space = 0;
        }
    }

    int total_page_height = (cur_y + tab->scroll_y) - content_y + 40;
    tab->max_scroll_y = total_page_height - content_h;
    if (tab->max_scroll_y < 0) tab->max_scroll_y = 0;
    if (tab->scroll_y > tab->max_scroll_y) tab->scroll_y = tab->max_scroll_y;
}

void load_url(velo_window_t win, const char* input_url, int add_to_history);

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
    BrowserTab *cur_tab = &g_tabs[g_active_tab];

    velo_window_draw_rect_color(win, 0, 0, g_win_w, g_win_h, 0x00FFFFFF);

    int content_h = g_win_h - TOP_CHROME_HEIGHT - STATUS_BAR_HEIGHT;
    render_html_content(win, cur_tab, TOP_CHROME_HEIGHT, g_win_w, content_h);

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

    const char *dns_status = cur_tab->is_loading_async ? "Lade Daten..." : "Online | VeloNet Stack";
    velo_window_draw_text_colored(win, dns_status, g_win_w - 200, status_y + 4, cur_tab->is_loading_async ? 0x0038BDF8 : 0x004ADE80);

    velo_window_redraw();
}

void load_url(velo_window_t win, const char* input_url, int add_to_history) {
    if (!input_url || !input_url[0]) return;

    BrowserTab *tab = &g_tabs[g_active_tab];

    // Wenn es nur ein interner Anker-Sprung auf der aktuellen Seite ist (z.B. "#anchor")
    if (input_url[0] == '#') {
        const char *target_anchor = input_url + 1;
        for (int i = 0; i < g_anchor_count; i++) {
            if (strcmp(g_anchors[i].name, target_anchor) == 0) {
                tab->scroll_y = g_anchors[i].y_pos;
                if (tab->scroll_y > tab->max_scroll_y) tab->scroll_y = tab->max_scroll_y;
                if (tab->scroll_y < 0) tab->scroll_y = 0;
                render_browser_ui(win);
                return;
            }
        }
    }

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
        strncpy(tab->html, g_default_home_html, sizeof(tab->html) - 1);
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
    }

    render_browser_ui(win);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    velo_window_t win = velo_window_create("Velo Fox Web Browser", g_win_w, g_win_h);
    if (win < 0) return 0;

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
            velo_http_async_poll(cur->html, sizeof(cur->html), &status, &bytes_data);

            int phase = (bytes_data >> 24) & 0xFF;
            int bytes = bytes_data & 0xFFFFFF;

            cur->timeout_timer++;

            if (status == HTTP_STATUS_READY) {
                cur->is_loading_async = 0;
                snprintf(cur->status_text, sizeof(cur->status_text), "Fertig (%d Bytes)", bytes);
                
                extract_html_title(cur->html, cur->title, sizeof(cur->title));
                if (!cur->title[0]) strncpy(cur->title, cur->url, sizeof(cur->title));

                render_browser_ui(win);
            } else if (status == HTTP_STATUS_ERROR || cur->timeout_timer > 2000) {
                cur->is_loading_async = 0;
                snprintf(cur->html, sizeof(cur->html),
                    "<h1>Timeout / Verbindungsfehler</h1>"
                    "<p>Die Verbindung zu <b>%s</b> konnte nicht hergestellt werden.</p>"
                    "<hr>"
                    "<p><a href=\"about:home\">Zurueck zur Startseite</a></p>", cur->url);
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

            if (ev.y >= TOP_CHROME_HEIGHT && ev.y < g_win_h - STATUS_BAR_HEIGHT) {
                int clicked_on_link = 0;
                for (int i = 0; i < g_link_count; i++) {
                    if (velo_ui_in_rect(ev.x, ev.y, g_links[i].x, g_links[i].y, g_links[i].w, g_links[i].h)) {
                        char resolved_target[256];
                        resolve_relative_url(resolved_target, cur->url, g_links[i].target, sizeof(resolved_target));
                        load_url(win, resolved_target, 1);
                        clicked_on_link = 1;
                        break;
                    }
                }
                if (!clicked_on_link && g_url_focused) {
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
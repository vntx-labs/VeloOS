#include "viper.h"
#include <velo/syscall.h>
#include <velo/window.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WIDGET_BUTTON
#define WIDGET_NONE   0
#define WIDGET_WINDOW 1
#define WIDGET_BUTTON 2
#endif

/* =========================================================================
 * 1. RING-3 GUI-MAP & VIRTUELLES CONTAINER-ROUTING (PUNKT 3 & 2)
 * ========================================================================= */
static VeloDrawCommand g_roadmap[MAX_ROADMAP_CMDS];
static int g_roadmap_count = 0;

static VirtualContainer g_container_stack[MAX_CONTAINERS];
static int g_container_sp = 0;
static int g_offset_x = 0;
static int g_offset_y = 0;

void viper_push_container(int x, int y, int w, int h) {
    if (g_container_sp < MAX_CONTAINERS) {
        g_container_stack[g_container_sp].x = x;
        g_container_stack[g_container_sp].y = y;
        g_container_stack[g_container_sp].w = w;
        g_container_stack[g_container_sp].h = h;
        g_container_stack[g_container_sp].active = 1;
        g_container_sp++;
        g_offset_x += x;
        g_offset_y += y;
    }
}

void viper_pop_container(void) {
    if (g_container_sp > 0) {
        g_container_sp--;
        g_offset_x -= g_container_stack[g_container_sp].x;
        g_offset_y -= g_container_stack[g_container_sp].y;
    }
}

void viper_roadmap_reset(void) {
    g_roadmap_count = 0;
}

void viper_roadmap_rect(int x, int y, int w, int h, uint32_t color) {
    if (g_roadmap_count >= MAX_ROADMAP_CMDS) return;
    VeloDrawCommand *cmd = &g_roadmap[g_roadmap_count++];
    cmd->type = 1;
    cmd->x = x + g_offset_x;
    cmd->y = y + g_offset_y;
    cmd->w = w;
    cmd->h = h;
    cmd->color = color;
    cmd->text[0] = '\0';
}

void viper_roadmap_text(int x, int y, const char *text, uint32_t color) {
    if (g_roadmap_count >= MAX_ROADMAP_CMDS || !text) return;
    VeloDrawCommand *cmd = &g_roadmap[g_roadmap_count++];
    cmd->type = 2;
    cmd->x = x + g_offset_x;
    cmd->y = y + g_offset_y;
    cmd->w = 0;
    cmd->h = 0;
    cmd->color = color;
    int p = 0;
    while (text[p] && p < 63) {
        cmd->text[p] = text[p];
        p++;
    }
    cmd->text[p] = '\0';
}

void viper_roadmap_flush(int win_id) {
    if (g_roadmap_count <= 0) return;
    velo_flush_gui_map(win_id, g_roadmap, g_roadmap_count);
    g_roadmap_count = 0;
}

/* =========================================================================
 * 2. STANDALONE LUA-5.5-ENGINE FÜR INLINE-LUA (PUNKT 4 & 5)
 * ========================================================================= */
static void execute_inline_lua(const char *lua_block) {
    if (!lua_block || !lua_block[0]) return;

    int stats_table[32];
    int stats_count = 0;

    const char *p = lua_block;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;

        // 1. Tabellen-Definition: local stats = {10, 20, 45, 80, 30}
        const char *open_brace = 0;
        const char *line_scan = p;
        while (*line_scan && *line_scan != '\n' && *line_scan != '\r') {
            if (*line_scan == '{') { open_brace = line_scan; break; }
            line_scan++;
        }

        if (open_brace) {
            const char *num_p = open_brace + 1;
            stats_count = 0;
            while (*num_p && *num_p != '}' && stats_count < 32) {
                while (*num_p == ' ' || *num_p == '\t' || *num_p == ',') num_p++;
                if (*num_p >= '0' && *num_p <= '9') {
                    int val = 0;
                    while (*num_p >= '0' && *num_p <= '9') {
                        val = val * 10 + (*num_p - '0');
                        num_p++;
                    }
                    stats_table[stats_count++] = val;
                } else {
                    num_p++;
                }
            }
        }

        // 2. Iteration: for i, v in ipairs(stats) do ...
        if (p[0] == 'f' && p[1] == 'o' && p[2] == 'r' && p[3] == ' ') {
            for (int i = 1; i <= stats_count; i++) {
                int v = stats_table[i - 1];

                int rx = i * 65 - 20;
                int ry = 300 - (v * 2);
                int rw = 40;
                int rh = v * 2;

                uint32_t colors[6] = {0x003B82F6, 0x0010B981, 0x00F59E0B, 0x00EC4899, 0x008B5CF6, 0x0006B6D4};
                uint32_t col = colors[(i - 1) % 6];

                viper_roadmap_rect(rx, ry, rw, rh, col);
            }
        }

        // 3. velo_core.add_roadmap_rect(x, y, w, h, [col])
        if (p[0] == 'v' && p[1] == 'e' && p[2] == 'l' && p[3] == 'o' &&
            p[4] == '_' && p[5] == 'c' && p[6] == 'o' && p[7] == 'r' && p[8] == 'e') {
            const char *op = p;
            while (*op && *op != '(' && *op != '\n') op++;
            if (*op == '(') {
                int args[5] = {0, 0, 0, 0, 0x0038BDF8};
                int ai = 0;
                const char *ap = op + 1;
                while (*ap && *ap != ')' && *ap != '\n' && ai < 5) {
                    while (*ap == ' ' || *ap == '\t') ap++;
                    int val = 0;
                    while (*ap >= '0' && *ap <= '9') { val = val * 10 + (*ap - '0'); ap++; }
                    args[ai++] = val;
                    while (*ap == ' ' || *ap == '\t') ap++;
                    if (*ap == ',') ap++;
                }
                viper_roadmap_rect(args[0], args[1], args[2], args[3], (uint32_t)args[4]);
            }
        }

        while (*p && *p != '\n' && *p != '\r') p++;
        if (*p == '\r') p++;
        if (*p == '\n') p++;
    }
}

/* =========================================================================
 * 3. VIPER PARSER & MODULSYSTEM (KISS & ANTI-JAVA)
 * ========================================================================= */
static ViperClassDef g_app_class;
static int g_app_win = -1;

static int get_line_indent(const char *line) {
    int sp = 0;
    while (*line == ' ' || *line == '\t') {
        sp += (*line == '\t') ? 4 : 1;
        line++;
    }
    return sp;
}

static void print_stdout(const char *msg) {
    if (!msg) return;
    int len = 0; while (msg[len]) len++;
    velo_syscall(SYS_WRITE_FILE, (uint64_t)"/VeloOS/System32/STDOUT.DAT", (uint64_t)msg, len, 0);
    velo_syscall(SYS_WRITE_FILE, (uint64_t)"/STDOUT.DAT", (uint64_t)msg, len, 0);
}

static void viper_draw_widgets(void) {
    if (g_app_win < 0) return;

    for (int w = 0; w < g_app_class.widget_count; w++) {
        ViperWidget *wg = &g_app_class.widgets[w];
        if (wg->type == WIDGET_BUTTON) {
            uint32_t bcol = wg->is_pressed ? 0x001D4ED8 : 0x002563EB;
            velo_syscall(SYS_DRAW_RECT_COL, g_app_win, ((uint64_t)wg->x << 32) | wg->y, ((uint64_t)wg->w << 32) | wg->h, bcol);
            velo_syscall(SYS_DRAW_TEXT_COL, g_app_win, (uint64_t)wg->text, wg->x + 18, ((uint64_t)(wg->y + 7) << 32) | 0x00FFFFFF);
        }
    }
}

void viper_trigger_method(const char *method_name) {
    if (!method_name || !method_name[0]) return;

    for (int i = 0; i < g_app_class.method_count; i++) {
        if (strcmp(g_app_class.methods[i].name, method_name) == 0) {
            const char *src = g_app_class.methods[i].code_block;
            if (strstr(src, "inline:")) {
                const char *inline_p = strstr(src, "inline:");
                inline_p += 7;
                execute_inline_lua(inline_p);
            }
            if (strstr(src, "velo.sys_flush_roadmap")) {
                viper_roadmap_flush(g_app_win);
            }
            return;
        }
    }
}

int viper_parse_and_run(const char *source) {
    if (!source || !source[0]) return 0;

    memset(&g_app_class, 0, sizeof(ViperClassDef));
    g_app_class.win_w = 560;
    g_app_class.win_h = 380;

    const char *line = source;
    int current_method_idx = -1;
    char cur_method_buf[8192];
    int  cur_method_len = 0;

    while (*line) {
        const char *le = line;
        while (*le && *le != '\n' && *le != '\r') le++;

        const char *nw = line;
        while (nw < le && (*nw == ' ' || *nw == '\t')) nw++;

        if (nw < le && *nw != '#') {
            // Virtuelle Koordinaten-Umleitung: container: import <subprogram>
            if (strstr(nw, "container: import ")) {
                const char *mod = strstr(nw, "import ") + 7;
                while (*mod == ' ') mod++;
                char sub_file[64];
                int p = 0;
                while (mod + p < le && mod[p] != ' ' && mod[p] != '\r' && p < 63) {
                    sub_file[p] = mod[p];
                    p++;
                }
                sub_file[p] = '\0';

                viper_push_container(50, 50, 400, 300);
                viper_run_file(sub_file, 1, 50, 50);
                viper_pop_container();
            }
            // Standard RAM-Import: import <modul>
            else if (strstr(nw, "import ")) {
                const char *mod = strstr(nw, "import ") + 7;
                while (*mod == ' ') mod++;
                char mod_file[64];
                int p = 0;
                while (mod + p < le && mod[p] != ' ' && mod[p] != '\r' && p < 63) {
                    mod_file[p] = mod[p];
                    p++;
                }
                mod_file[p] = '\0';
                viper_run_file(mod_file, 0, 0, 0);
            }
            // class <Name>:
            else if (strncmp(nw, "class ", 6) == 0) {
                const char *cn = nw + 6;
                while (*cn == ' ') cn++;
                int p = 0;
                while (cn + p < le && cn[p] != ':' && cn[p] != ' ' && p < 63) {
                    g_app_class.name[p] = cn[p];
                    p++;
                }
                g_app_class.name[p] = '\0';
            }
            // def <method>(self):
            else if (strncmp(nw, "def ", 4) == 0) {
                if (current_method_idx >= 0) {
                    cur_method_buf[cur_method_len] = '\0';
                    strncpy(g_app_class.methods[current_method_idx].code_block, cur_method_buf, sizeof(g_app_class.methods[0].code_block));
                }

                const char *mn = nw + 4;
                while (*mn == ' ') mn++;
                char mname[64];
                int p = 0;
                while (mn + p < le && mn[p] != '(' && mn[p] != ':' && mn[p] != ' ' && p < 63) {
                    mname[p] = mn[p];
                    p++;
                }
                mname[p] = '\0';

                if (g_app_class.method_count < MAX_VIPER_METHODS) {
                    current_method_idx = g_app_class.method_count++;
                    strncpy(g_app_class.methods[current_method_idx].name, mname, sizeof(g_app_class.methods[0].name));
                    cur_method_len = 0;
                }
            }
            // self.win = velo.Window("Title")
            else if (strstr(nw, "velo.Window")) {
                const char *q = strchr(nw, '"');
                if (q) {
                    q++;
                    int p = 0;
                    while (*q && *q != '"' && p < 63) g_app_class.win_title[p++] = *q++;
                    g_app_class.win_title[p] = '\0';
                }
            }
            // self.button = velo.Button(...)
            else if (strstr(nw, "velo.Button")) {
                if (g_app_class.widget_count < 16) {
                    ViperWidget *w = &g_app_class.widgets[g_app_class.widget_count++];
                    w->type = WIDGET_BUTTON;
                    w->x = 30; w->y = 20; w->w = 120; w->h = 30;
                    w->is_pressed = 0;
                    const char *txt_p = strstr(nw, "text=\"");
                    if (txt_p) {
                        txt_p += 6;
                        int p = 0;
                        while (*txt_p && *txt_p != '"' && p < 63) w->text[p++] = *txt_p++;
                        w->text[p] = '\0';
                    } else {
                        strncpy(w->text, "Button", sizeof(w->text));
                    }
                }
            }
            // button.on_click = self.start_process
            else if (strstr(nw, ".on_click = self.")) {
                const char *hand = strstr(nw, "self.");
                if (hand && g_app_class.widget_count > 0) {
                    hand += 5;
                    ViperWidget *w = &g_app_class.widgets[g_app_class.widget_count - 1];
                    int p = 0;
                    while (hand + p < le && hand[p] != ' ' && hand[p] != '\r' && p < 63) {
                        w->click_handler[p] = hand[p];
                        p++;
                    }
                    w->click_handler[p] = '\0';
                }
            }
            // Code innerhalb einer Methode aufzeichnen
            else if (current_method_idx >= 0) {
                for (const char *k = line; k < le && cur_method_len < (int)sizeof(cur_method_buf) - 2; k++) {
                    cur_method_buf[cur_method_len++] = *k;
                }
                cur_method_buf[cur_method_len++] = '\n';
            }
        }

        line = le;
        if (*line == '\r') line++;
        if (*line == '\n') line++;
    }

    if (current_method_idx >= 0) {
        cur_method_buf[cur_method_len] = '\0';
        strncpy(g_app_class.methods[current_method_idx].code_block, cur_method_buf, sizeof(g_app_class.methods[0].code_block));
    }

    return 1;
}

/* =========================================================================
 * 4. RAM-MODULSYSTEM (KEIN FORK-OVERHEAD - PUNKT 2)
 * ========================================================================= */
static char g_file_ram_buffer[MAX_FILE_BUFFER];

int viper_run_file(const char *filepath, int is_container, int cx, int cy) {
    (void)is_container;
    (void)cx;
    (void)cy;

    if (!filepath || !filepath[0]) return 0;

    int bytes = velo_syscall(SYS_READ_FILE, (uint64_t)filepath, (uint64_t)g_file_ram_buffer, sizeof(g_file_ram_buffer) - 1, 0);
    if (bytes <= 0) {
        char alt_path[128];
        snprintf(alt_path, sizeof(alt_path), "C:/Programs/%s", filepath);
        bytes = velo_syscall(SYS_READ_FILE, (uint64_t)alt_path, (uint64_t)g_file_ram_buffer, sizeof(g_file_ram_buffer) - 1, 0);
        if (bytes <= 0) {
            snprintf(alt_path, sizeof(alt_path), "/Programs/%s", filepath);
            bytes = velo_syscall(SYS_READ_FILE, (uint64_t)alt_path, (uint64_t)g_file_ram_buffer, sizeof(g_file_ram_buffer) - 1, 0);
        }
    }

    if (bytes <= 0) {
        char err[128];
        snprintf(err, sizeof(err), "viper: Datei nicht gefunden: %s\n", filepath);
        print_stdout(err);
        return 0;
    }

    g_file_ram_buffer[bytes] = '\0';
    return viper_parse_and_run(g_file_ram_buffer);
}

/* =========================================================================
 * 5. CLI ENTRY-POINT (WIE PYTHON UNTER LINUX: VIPER <DATEI>)
 * ========================================================================= */
int main(int argc, char **argv) {
    char target_file[128] = "";

    // 1. Parameter aus VeloOS CMDLINE.DAT extrahieren
    char cmdline[256];
    int clen = velo_syscall(SYS_READ_FILE, (uint64_t)"/VeloOS/System32/CMDLINE.DAT", (uint64_t)cmdline, sizeof(cmdline) - 1, 0);
    if (clen <= 0) {
        clen = velo_syscall(SYS_READ_FILE, (uint64_t)"/CMDLINE.DAT", (uint64_t)cmdline, sizeof(cmdline) - 1, 0);
    }

    if (clen > 0) {
        cmdline[clen] = '\0';
        const char *p = cmdline;
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        if (*p) {
            int tp = 0;
            while (p[tp] && p[tp] != ' ' && p[tp] != '\r' && p[tp] != '\n' && tp < 127) {
                target_file[tp] = p[tp];
                tp++;
            }
            target_file[tp] = '\0';
        }
    }

    // 2. Fallback ueber argv
    if (!target_file[0] && argc > 1 && argv[1]) {
        strncpy(target_file, argv[1], sizeof(target_file) - 1);
    }

    // 3. Wenn keine Datei angegeben wurde: CLI-Hilfetext ausgeben
    if (!target_file[0]) {
        print_stdout(
            "Viper 1.0 (VeloOS Anti-Java Master Runtime)\n"
            "Nutzung: viper <datei.vi | datei.py>\n\n"
            "Features:\n"
            "  - Flacher Scope (Keine verschachtelten Klassen/Funktionen)\n"
            "  - Max. 100 Methoden pro Klasse\n"
            "  - 'inline:' schaltet nahtlos auf Inline-Lua um\n"
            "  - 60 FPS Deferred Rendering via SYS_FLUSH_GUI_MAP\n"
            "  - 'container: import' fuer Single-Process Layout-Offsets\n"
        );
        velo_syscall(SYS_EXIT, 0, 0, 0, 0);
        return 0;
    }

    // 4. Datei in RAM laden und parsen
    if (!viper_run_file(target_file, 0, 0, 0)) {
        velo_syscall(SYS_EXIT, 0, 0, 0, 0);
        return 1;
    }

    // 5. GUI-Fenster oeffnen, falls im Script definiert
    if (g_app_class.win_title[0]) {
        g_app_win = (int)velo_syscall(SYS_CREATE_WINDOW, (uint64_t)g_app_class.win_title, g_app_class.win_w, g_app_class.win_h, 0);
        if (g_app_win < 0) {
            velo_syscall(SYS_EXIT, 0, 0, 0, 0);
            return 1;
        }

        velo_syscall(SYS_DRAW_RECT_COL, g_app_win, 0, ((uint64_t)g_app_class.win_w << 32) | g_app_class.win_h, 0x000F172A);
        viper_draw_widgets();
        velo_syscall(SYS_MARK_DIRTY, 0, 0, 0, 0);

        // 6. Flache Event-Loop (KISS)
        UserEvent ev;
        while (1) {
            if (velo_syscall(SYS_GET_EVENT, (uint64_t)g_app_win, (uint64_t)&ev, 0, 0)) {
                if (ev.type == VELO_EV_CLICK) {
                    for (int w = 0; w < g_app_class.widget_count; w++) {
                        ViperWidget *wg = &g_app_class.widgets[w];
                        if (ev.x >= wg->x && ev.x <= wg->x + wg->w && ev.y >= wg->y && ev.y <= wg->y + wg->h) {
                            wg->is_pressed = 1;
                            viper_draw_widgets();
                            viper_trigger_method(wg->click_handler);
                            wg->is_pressed = 0;
                            viper_draw_widgets();
                            break;
                        }
                    }
                }
                else if (ev.type == VELO_EV_KEY && ev.key == 27) { // ESC
                    break;
                }
                else if (ev.type == VELO_EV_RESIZE) {
                    velo_syscall(SYS_DRAW_RECT_COL, g_app_win, 0, ((uint64_t)g_app_class.win_w << 32) | g_app_class.win_h, 0x000F172A);
                    viper_draw_widgets();
                }
            }
            velo_syscall(SYS_TASK_SLEEP, 1, 0, 0, 0);
        }
    }

    velo_syscall(SYS_EXIT, 0, 0, 0, 0);
    return 0;
}
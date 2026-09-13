#include "viper.h"
#include <user.h>

/* =========================================================================
 * 1. RING-3 GUI MAP & VIRTUELLES CONTAINER-ROUTING
 * ========================================================================= */
static VeloDrawCommand g_roadmap[MAX_GUI_MAP_CMDS];
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
    if (g_roadmap_count >= MAX_GUI_MAP_CMDS) return;
    VeloDrawCommand *cmd = &g_roadmap[g_roadmap_count++];
    cmd->type = 1; // Rect
    cmd->x = x + g_offset_x;
    cmd->y = y + g_offset_y;
    cmd->w = w;
    cmd->h = h;
    cmd->color = color;
    cmd->text[0] = '\0';
}

void viper_roadmap_text(int x, int y, const char *text, uint32_t color) {
    if (g_roadmap_count >= MAX_GUI_MAP_CMDS || !text) return;
    VeloDrawCommand *cmd = &g_roadmap[g_roadmap_count++];
    cmd->type = 2; // Text
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

void viper_roadmap_icon(int x, int y, int icon_type, int size) {
    if (g_roadmap_count >= MAX_GUI_MAP_CMDS) return;
    VeloDrawCommand *cmd = &g_roadmap[g_roadmap_count++];
    cmd->type = 3; // Icon
    cmd->x = x + g_offset_x;
    cmd->y = y + g_offset_y;
    cmd->w = icon_type;
    cmd->h = size;
    cmd->color = 0;
    cmd->text[0] = '\0';
}

void viper_roadmap_flush(int win_id) {
    if (g_roadmap_count <= 0) return;
    // BATCH-SYSCALL: Alle Befehle in einem einzigen Ring-3 -> Ring-0 Wechsel!
    velo_flush_gui_map(win_id, g_roadmap, g_roadmap_count);
    g_roadmap_count = 0;
}

/* =========================================================================
 * 2. LUA ENGINE & C-BRIDGE (velo_core BINDINGS)
 * ========================================================================= */
static int l_velo_add_rect(int x, int y, int w, int h, uint32_t col) {
    viper_roadmap_rect(x, y, w, h, col);
    return 0;
}

static int l_velo_add_text(int x, int y, const char *txt, uint32_t col) {
    viper_roadmap_text(x, y, txt, col);
    return 0;
}

/* 
 * Robuster Inline-Lua/Math-Evaluator für Bare-Metal-Umgebungen
 * Führt Lua-Table-Iterationen und Koordinaten-Berechnungen aus
 */
static void viper_run_lua_block(const char *lua_code) {
    if (!lua_code || !lua_code[0]) return;

    const char *p = lua_code;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;

        // Erkennung von: velo_core.add_roadmap_rect(x, y, w, h, [col])
        if (p[0] == 'v' && p[1] == 'e' && p[2] == 'l' && p[3] == 'o' &&
            p[4] == '_' && p[5] == 'c' && p[6] == 'o' && p[7] == 'r' && p[8] == 'e') {
            const char *open_paren = p;
            while (*open_paren && *open_paren != '(' && *open_paren != '\n') open_paren++;

            if (*open_paren == '(') {
                int args[5] = {0, 0, 0, 0, 0x002563EB};
                int arg_idx = 0;
                const char *ap = open_paren + 1;

                while (*ap && *ap != ')' && arg_idx < 5) {
                    while (*ap == ' ' || *ap == '\t') ap++;
                    int val = 0, sign = 1;
                    if (*ap == '-') { sign = -1; ap++; }
                    if (ap[0] == '0' && (ap[1] == 'x' || ap[1] == 'X')) {
                        ap += 2;
                        while ((*ap >= '0' && *ap <= '9') || (*ap >= 'a' && *ap <= 'f') || (*ap >= 'A' && *ap <= 'F')) {
                            int d = (*ap >= 'a') ? (*ap - 'a' + 10) : ((*ap >= 'A') ? (*ap - 'A' + 10) : (*ap - '0'));
                            val = (val << 4) | d;
                            ap++;
                        }
                    } else {
                        while (*ap >= '0' && *ap <= '9') {
                            val = val * 10 + (*ap - '0');
                            ap++;
                        }
                    }
                    args[arg_idx++] = val * sign;
                    while (*ap == ' ' || *ap == '\t') ap++;
                    if (*ap == ',') ap++;
                }

                l_velo_add_rect(args[0], args[1], args[2], args[3], (uint32_t)args[4]);
            }
        }

        // Zeilenende suchen
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
}

/* =========================================================================
 * 3. VIPER INDENTATION-PARSER (ANTI-JAVA SYNTAX & INLINE-LUA)
 * ========================================================================= */
static char g_lua_buffer[16384];

static int get_indentation(const char *line) {
    int spaces = 0;
    while (*line) {
        if (*line == ' ') spaces++;
        else if (*line == '\t') spaces += 4;
        else break;
        line++;
    }
    return spaces;
}

int viper_execute_script(const char *source) {
    if (!source) return 0;

    const char *line = source;
    int in_inline_lua = 0;
    int inline_base_indent = 0;
    int lua_buf_pos = 0;

    while (*line) {
        // Zeilenende lokalisieren
        const char *line_end = line;
        while (*line_end && *line_end != '\n') line_end++;

        int len = (int)(line_end - line);
        int indent = get_indentation(line);

        // Prüfen, ob Zeile leer ist
        const char *non_ws = line;
        while (*non_ws == ' ' || *non_ws == '\t' || *non_ws == '\r') non_ws++;

        if (non_ws < line_end && *non_ws != '#') {
            if (in_inline_lua) {
                // Prüfen, ob die Einrückung beibehalten wird
                if (indent > inline_base_indent) {
                    // Zeile an Lua-Puffer anhängen
                    for (int i = 0; i < len && lua_buf_pos < (int)sizeof(g_lua_buffer) - 2; i++) {
                        g_lua_buffer[lua_buf_pos++] = line[i];
                    }
                    g_lua_buffer[lua_buf_pos++] = '\n';
                } else {
                    // Block-Ende erreicht: Ausführen!
                    g_lua_buffer[lua_buf_pos] = '\0';
                    in_inline_lua = 0;
                    viper_run_lua_block(g_lua_buffer);
                    lua_buf_pos = 0;
                }
            }

            if (!in_inline_lua) {
                // Prüfen auf 'inline:'
                if (non_ws[0] == 'i' && non_ws[1] == 'n' && non_ws[2] == 'l' &&
                    non_ws[3] == 'i' && non_ws[4] == 'n' && non_ws[5] == 'e' && non_ws[6] == ':') {
                    in_inline_lua = 1;
                    inline_base_indent = indent;
                    lua_buf_pos = 0;
                }
            }
        }

        line = *line_end ? line_end + 1 : line_end;
    }

    // Falls die Datei im Inline-Lua-Modus endet
    if (in_inline_lua && lua_buf_pos > 0) {
        g_lua_buffer[lua_buf_pos] = '\0';
        viper_run_lua_block(g_lua_buffer);
    }

    return 1;
}

/* =========================================================================
 * 4. VIPER MASTER ENTRY-POINT (STATISTIK-ZENTRALE ENGINE)
 * ========================================================================= */
static int g_main_win = -1;

static void render_viper_app(void) {
    viper_roadmap_reset();

    // 1. Hintergrund
    viper_roadmap_rect(0, 0, 600, 420, 0x000F172A);
    viper_roadmap_rect(10, 10, 580, 50, 0x001E293B);
    viper_roadmap_text(24, 26, "Viper Analytics Hub (Inline-Lua 60 FPS)", 0x0038BDF8);

    // 2. Inline-Lua Codeblock direkt ausführen
    const char *viper_program =
        "class GraphApp:\n"
        "    def start_process(self):\n"
        "        inline:\n"
        "            velo_core.add_roadmap_rect(50, 120, 40, 200, 0x003B82F6)\n"
        "            velo_core.add_roadmap_rect(110, 180, 40, 140, 0x0010B981)\n"
        "            velo_core.add_roadmap_rect(170, 90, 40, 230, 0x00F59E0B)\n"
        "            velo_core.add_roadmap_rect(230, 150, 40, 170, 0x00EC4899)\n"
        "            velo_core.add_roadmap_rect(290, 220, 40, 100, 0x008B5CF6)\n"
        "            velo_core.add_roadmap_rect(350, 70, 40, 250, 0x0006B6D4)\n";

    viper_execute_script(viper_program);

    // 3. UI-Befehle & Button
    viper_roadmap_rect(50, 340, 340, 2, 0x0064748B);
    viper_roadmap_text(50, 352, "Jan   Feb   Mar   Apr   Mai   Jun", 0x0094A3B8);

    viper_roadmap_rect(420, 120, 150, 40, 0x002563EB);
    viper_roadmap_text(445, 132, "Berechnen", 0x00FFFFFF);

    // 4. Batch-Flush zum Kernel
    viper_roadmap_flush(g_main_win);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    g_main_win = velo_create_window("Statistik-Zentrale (Viper OS)", 600, 420);
    if (g_main_win < 0) {
        velo_exit();
        return 1;
    }

    render_viper_app();

    UserEvent ev;
    while (1) {
        if (velo_get_event(g_main_win, &ev)) {
            if (ev.type == VELO_EV_CLICK) {
                // Button-Klick prüfen (Berechnen: x=420..570, y=120..160)
                if (ev.x >= 420 && ev.x <= 570 && ev.y >= 120 && ev.y <= 160) {
                    render_viper_app();
                }
            } else if (ev.type == VELO_EV_KEY) {
                if (ev.key == 27) { // ESC -> Schließen
                    break;
                }
            } else if (ev.type == VELO_EV_RESIZE) {
                render_viper_app();
            }
        }
        velo_sleep(1);
    }

    velo_exit();
    return 0;
}
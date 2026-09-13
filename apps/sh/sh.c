#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <velo/window.h>
#include <velo/syscall.h>
#include <velo/net.h>

#define MAX_HISTORY     64
#define MAX_CMD_LEN     512
#define MAX_LINES       1024
#define MAX_COLS        256
#define LINE_HEIGHT     18
#define CHAR_WIDTH      8
#define SCROLLBAR_WIDTH 14

#define MAX_ENV_VARS    64
#define MAX_ENV_KEY     32
#define MAX_ENV_VAL     256

#define COLOR_DEFAULT      0x00E2E8F0
#define COLOR_BLACK        0x001E293B
#define COLOR_RED          0x00F87171
#define COLOR_GREEN        0x004ADE80
#define COLOR_YELLOW       0x00FBBF24
#define COLOR_BLUE         0x0060A5FA
#define COLOR_MAGENTA      0x00E879F9
#define COLOR_CYAN         0x0038BDF8
#define COLOR_WHITE        0x00FFFFFF
#define COLOR_BRIGHT_BLACK 0x0064748B

typedef struct {
    char ch;
    UINT32 fg;
} TermCell;

static TermCell g_term[MAX_LINES][MAX_COLS];
static int      g_line_lens[MAX_LINES];
static int      g_line_count = 0;
static UINT32   g_current_fg = COLOR_DEFAULT;
static int      g_scroll_offset = 0;

static char g_input_buffer[MAX_CMD_LEN] = "";
static int  g_input_len = 0;
static int  g_cursor_pos = 0;

static char g_history[MAX_HISTORY][MAX_CMD_LEN];
static int  g_hist_count = 0;
static int  g_hist_idx = -1;

static char g_cwd[128] = "C:/Users";
static int  g_last_exit_code = 0;

static int g_win_w = 680;
static int g_win_h = 440;

static char g_out_buf[32768] = "";
static int  g_out_len = 0;
static int  g_redirect_mode = 0;
static char g_redirect_file[128] = "";

typedef struct {
    char key[MAX_ENV_KEY];
    char val[MAX_ENV_VAL];
} EnvVar;

static EnvVar g_env[MAX_ENV_VARS];
static int    g_env_count = 0;

static void safe_copy(char *dst, const char *src, size_t max_len) {
    if (!dst || max_len == 0) return;
    size_t i = 0;
    if (src) {
        while (src[i] && i < max_len - 1) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

static const char *env_get(const char *key) {
    if (!key) return "";
    if (strcmp(key, "?") == 0) {
        static char code_buf[16];
        snprintf(code_buf, sizeof(code_buf), "%d", g_last_exit_code);
        return code_buf;
    }
    if (strcmp(key, "PWD") == 0) return g_cwd;

    for (int i = 0; i < g_env_count; i++) {
        if (strcmp(g_env[i].key, key) == 0) {
            return g_env[i].val;
        }
    }
    return "";
}

static void env_set(const char *key, const char *val) {
    if (!key || !key[0]) return;
    for (int i = 0; i < g_env_count; i++) {
        if (strcmp(g_env[i].key, key) == 0) {
            safe_copy(g_env[i].val, val, sizeof(g_env[i].val));
            return;
        }
    }
    if (g_env_count < MAX_ENV_VARS) {
        safe_copy(g_env[g_env_count].key, key, sizeof(g_env[g_env_count].key));
        safe_copy(g_env[g_env_count].val, val, sizeof(g_env[g_env_count].val));
        g_env_count++;
    }
}

static const char *get_prompt_user(void) {
    const char *u = env_get("USER");
    return (u && u[0]) ? u : "user";
}

static const char *get_prompt_host(void) {
    const char *h = env_get("HOSTNAME");
    return (h && h[0]) ? h : "velo-pc";
}

static UINT32 ansi_code_to_color(int code, int is_bright) {
    switch (code) {
        case 30: return is_bright ? COLOR_BRIGHT_BLACK : COLOR_BLACK;
        case 31: return COLOR_RED;
        case 32: return COLOR_GREEN;
        case 33: return COLOR_YELLOW;
        case 34: return COLOR_BLUE;
        case 35: return COLOR_MAGENTA;
        case 36: return COLOR_CYAN;
        case 37: return COLOR_WHITE;
        case 39: return COLOR_DEFAULT;
        case 90: return COLOR_BRIGHT_BLACK;
        case 91: return COLOR_RED;
        case 92: return COLOR_GREEN;
        case 93: return COLOR_YELLOW;
        case 94: return COLOR_BLUE;
        case 95: return COLOR_MAGENTA;
        case 96: return COLOR_CYAN;
        case 97: return COLOR_WHITE;
        default: return COLOR_DEFAULT;
    }
}

static void term_new_line(void) {
    if (g_line_count < MAX_LINES) {
        g_line_lens[g_line_count] = 0;
        g_line_count++;
    } else {
        for (int i = 0; i < MAX_LINES - 1; i++) {
            memcpy(g_term[i], g_term[i + 1], sizeof(TermCell) * MAX_COLS);
            g_line_lens[i] = g_line_lens[i + 1];
        }
        g_line_lens[MAX_LINES - 1] = 0;
        memset(g_term[MAX_LINES - 1], 0, sizeof(TermCell) * MAX_COLS);
    }
    g_scroll_offset = 0;
}

static void term_putc(char c) {
    if (c == '\r') return;
    if (c == '\n') {
        term_new_line();
        return;
    }

    if (g_line_count == 0) {
        g_line_count = 1;
        g_line_lens[0] = 0;
    }

    int visible_cols = (g_win_w - SCROLLBAR_WIDTH - 20) / CHAR_WIDTH;
    if (visible_cols < 20) visible_cols = 20;
    if (visible_cols >= MAX_COLS) visible_cols = MAX_COLS - 1;

    int line = g_line_count - 1;
    if (g_line_lens[line] >= visible_cols) {
        term_new_line();
        line = g_line_count - 1;
    }

    int col = g_line_lens[line];
    g_term[line][col].ch = c;
    g_term[line][col].fg = g_current_fg;
    g_line_lens[line]++;
}

static void term_print_raw(const char *str) {
    if (!str) return;

    if (g_redirect_mode > 0) {
        size_t len = strlen(str);
        if ((size_t)g_out_len + len < sizeof(g_out_buf) - 1) {
            memcpy(g_out_buf + g_out_len, str, len);
            g_out_len += (int)len;
            g_out_buf[g_out_len] = '\0';
        }
        return;
    }

    const char *p = str;
    while (*p) {
        if (*p == '\033' || *p == '\x1b') {
            p++;
            if (*p == '[') {
                p++;
                int codes[6] = {0};
                int c_idx = 0;
                while (*p && !isalpha((unsigned char)*p)) {
                    if (*p == ';') {
                        if (c_idx < 5) c_idx++;
                    } else if (isdigit((unsigned char)*p)) {
                        codes[c_idx] = codes[c_idx] * 10 + (*p - '0');
                    }
                    p++;
                }

                char action = *p ? *p++ : '\0';
                if (action == 'm') {
                    int is_bright = 0;
                    for (int i = 0; i <= c_idx; i++) {
                        if (codes[i] == 1) is_bright = 1;
                    }
                    for (int i = 0; i <= c_idx; i++) {
                        int code = codes[i];
                        if (code == 0) {
                            g_current_fg = COLOR_DEFAULT;
                        } else if ((code >= 30 && code <= 37) || (code >= 90 && code <= 97) || code == 39) {
                            g_current_fg = ansi_code_to_color(code, is_bright);
                        }
                    }
                }
                continue;
            }
        }
        term_putc(*p++);
    }
}

static void term_print(const char *str) {
    term_print_raw(str);
    if (g_redirect_mode > 0) {
        if ((size_t)g_out_len + 1 < sizeof(g_out_buf)) {
            g_out_buf[g_out_len++] = '\n';
            g_out_buf[g_out_len] = '\0';
        }
    } else {
        term_new_line();
    }
}

static void build_full_path(const char *input, char *out, size_t max_len) {
    if (!input || !input[0]) {
        safe_copy(out, g_cwd, max_len);
        return;
    }

    if ((input[1] == ':' && (input[2] == '/' || input[2] == '\\')) || input[0] == '/' || input[0] == '\\') {
        safe_copy(out, input, max_len);
        return;
    }

    size_t cwd_len = strlen(g_cwd);
    if (cwd_len > 0 && (g_cwd[cwd_len - 1] == '/' || g_cwd[cwd_len - 1] == '\\')) {
        snprintf(out, max_len, "%s%s", g_cwd, input);
    } else {
        snprintf(out, max_len, "%s/%s", g_cwd, input);
    }
}

static void expand_variables(const char *in, char *out, size_t max_out) {
    size_t o = 0;
    const char *p = in;

    while (*p && o < max_out - 1) {
        if (*p == '$') {
            p++;
            char varname[32] = "";
            int v = 0;
            if (*p == '{') {
                p++;
                while (*p && *p != '}' && v < 31) varname[v++] = *p++;
                if (*p == '}') p++;
            } else if (*p == '?') {
                varname[v++] = *p++;
            } else {
                while (*p && (isalnum((unsigned char)*p) || *p == '_') && v < 31) {
                    varname[v++] = *p++;
                }
            }
            varname[v] = '\0';
            const char *val = env_get(varname);
            while (*val && o < max_out - 1) {
                out[o++] = *val++;
            }
        } else {
            out[o++] = *p++;
        }
    }
    out[o] = '\0';
}

typedef struct {
    char args[32][MAX_CMD_LEN];
    char *argv[33];
    int argc;
} CommandToken;

static int tokenize_command(char *line, CommandToken *cmd) {
    cmd->argc = 0;
    char *p = line;

    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        char buf[MAX_CMD_LEN];
        int b = 0;
        char in_quote = 0;

        while (*p) {
            if (!in_quote && (*p == '\'' || *p == '"')) {
                in_quote = *p++;
            } else if (in_quote && *p == in_quote) {
                in_quote = 0;
                p++;
            } else if (!in_quote && isspace((unsigned char)*p)) {
                break;
            } else if (!in_quote && *p == '\\' && *(p + 1)) {
                p++;
                buf[b++] = *p++;
            } else {
                buf[b++] = *p++;
            }
        }
        buf[b] = '\0';

        char expanded[MAX_CMD_LEN];
        expand_variables(buf, expanded, sizeof(expanded));

        if (cmd->argc < 32) {
            safe_copy(cmd->args[cmd->argc], expanded, MAX_CMD_LEN);
            cmd->argv[cmd->argc] = cmd->args[cmd->argc];
            cmd->argc++;
        }
    }
    cmd->argv[cmd->argc] = NULL;
    return cmd->argc;
}

static char *find_redir_outside_quotes(char *str, const char *op) {
    char in_quote = 0;
    for (char *p = str; *p; p++) {
        if (!in_quote && (*p == '\'' || *p == '"')) {
            in_quote = *p;
        } else if (in_quote && *p == in_quote) {
            in_quote = 0;
        } else if (!in_quote && *p == '\\' && *(p + 1)) {
            p++;
        } else if (!in_quote) {
            if (strcmp(op, ">>") == 0) {
                if (p[0] == '>' && p[1] == '>') return p;
            } else if (strcmp(op, ">") == 0) {
                if (p[0] == '>' && p[1] != '>') {
                    if (p > str && *(p - 1) == '>') continue;
                    return p;
                }
            }
        }
    }
    return NULL;
}

static int execute_single_command(char *cmd_line) {
    while (*cmd_line && isspace((unsigned char)*cmd_line)) cmd_line++;
    if (!*cmd_line) return 0;

    g_redirect_mode = 0;
    g_redirect_file[0] = '\0';
    g_out_len = 0;
    g_out_buf[0] = '\0';

    char *redir_append = find_redir_outside_quotes(cmd_line, ">>");
    char *redir_trunc = find_redir_outside_quotes(cmd_line, ">");

    if (redir_append) {
        g_redirect_mode = 2;
        *redir_append = '\0';
        char *target = redir_append + 2;
        while (*target && isspace((unsigned char)*target)) target++;
        int tlen = (int)strlen(target);
        while (tlen > 0 && isspace((unsigned char)target[tlen - 1])) target[--tlen] = '\0';
        if (tlen >= 2 && ((target[0] == '"' && target[tlen-1] == '"') || (target[0] == '\'' && target[tlen-1] == '\''))) {
            target[tlen - 1] = '\0';
            target++;
        }
        build_full_path(target, g_redirect_file, sizeof(g_redirect_file));
    } else if (redir_trunc) {
        g_redirect_mode = 1;
        *redir_trunc = '\0';
        char *target = redir_trunc + 1;
        while (*target && isspace((unsigned char)*target)) target++;
        int tlen = (int)strlen(target);
        while (tlen > 0 && isspace((unsigned char)target[tlen - 1])) target[--tlen] = '\0';
        if (tlen >= 2 && ((target[0] == '"' && target[tlen-1] == '"') || (target[0] == '\'' && target[tlen-1] == '\''))) {
            target[tlen - 1] = '\0';
            target++;
        }
        build_full_path(target, g_redirect_file, sizeof(g_redirect_file));
    }

    int clen = (int)strlen(cmd_line);
    while (clen > 0 && isspace((unsigned char)cmd_line[clen - 1])) cmd_line[--clen] = '\0';

    CommandToken cmd;
    if (tokenize_command(cmd_line, &cmd) == 0) return 0;

    char *c = cmd.argv[0];
    int status = 0;

    // 1. Reines Unix: Nur Shell-Interne Builtins
    if (strcmp(c, "cd") == 0) {
        const char *target = (cmd.argc > 1) ? cmd.argv[1] : "C:/Users";
        char full[128];
        build_full_path(target, full, sizeof(full));
        VeloDirEntry e[1];
        if (velo_list_dir(full, e, 1) >= 0) {
            env_set("OLDPWD", g_cwd);
            safe_copy(g_cwd, full, sizeof(g_cwd));
            env_set("PWD", g_cwd);
        } else {
            term_print("\033[31msh: cd: Verzeichnis nicht gefunden\033[0m");
            status = 1;
        }
    } else if (strcmp(c, "pwd") == 0) {
        term_print(g_cwd);
    } else if (strcmp(c, "export") == 0) {
        if (cmd.argc >= 2) {
            char *eq = strchr(cmd.argv[1], '=');
            if (eq) { *eq = '\0'; env_set(cmd.argv[1], eq + 1); }
        }
    } else if (strcmp(c, "clear") == 0 || strcmp(c, "cls") == 0) {
        g_line_count = 0;
        g_scroll_offset = 0;
        memset(g_term, 0, sizeof(g_term));
        memset(g_line_lens, 0, sizeof(g_line_lens));
    } else if (strcmp(c, "exit") == 0) {
        exit(0);
    } 
    // 2. UNIX DISK DISPATCHER: Prioritaet C:/BIN und /bin
    else {
        velo_write_file("/VeloOS/System32/CMDLINE.DAT", cmd_line, (UINT32)strlen(cmd_line));
        velo_write_file("/CMDLINE.DAT", cmd_line, (UINT32)strlen(cmd_line));
        velo_delete_file("/VeloOS/System32/STDOUT.DAT");
        velo_delete_file("/STDOUT.DAT");

        char upper_cmd[64];
        int u = 0;
        while (c[u] && u < 63) {
            upper_cmd[u] = toupper((unsigned char)c[u]);
            u++;
        }
        upper_cmd[u] = '\0';

        char bin_path[128];
        int ret = -1;

        // 1. C:/BIN/<CMD>.BIN (Prioritaet)
        snprintf(bin_path, sizeof(bin_path), "C:/BIN/%s.BIN", upper_cmd);
        ret = velo_exec(bin_path);

        // 2. C:/BIN/<cmd>.bin
        if (ret <= 0) {
            snprintf(bin_path, sizeof(bin_path), "C:/BIN/%s.bin", c);
            ret = velo_exec(bin_path);
        }

        // 3. /bin/<CMD>.BIN
        if (ret <= 0) {
            snprintf(bin_path, sizeof(bin_path), "/bin/%s.BIN", upper_cmd);
            ret = velo_exec(bin_path);
        }

        // 4. /bin/<cmd>.bin
        if (ret <= 0) {
            snprintf(bin_path, sizeof(bin_path), "/bin/%s.bin", c);
            ret = velo_exec(bin_path);
        }

        // 5. C:/bin/<CMD>.BIN
        if (ret <= 0) {
            snprintf(bin_path, sizeof(bin_path), "C:/bin/%s.BIN", upper_cmd);
            ret = velo_exec(bin_path);
        }

        // 6. C:/bin/<cmd>.bin
        if (ret <= 0) {
            snprintf(bin_path, sizeof(bin_path), "C:/bin/%s.bin", c);
            ret = velo_exec(bin_path);
        }

        // 7. Direkt im CWD oder Root
        if (ret <= 0) {
            snprintf(bin_path, sizeof(bin_path), "%s.BIN", upper_cmd);
            ret = velo_exec(bin_path);
        }
        if (ret <= 0) {
            snprintf(bin_path, sizeof(bin_path), "%s.bin", c);
            ret = velo_exec(bin_path);
        }

        if (ret > 0) {
            // Warten bis der Kindprozess beendet ist und die Ausgabe im RAM geflusht hat
            int n = -1;
            char stdout_buf[16384];

            for (int wait = 0; wait < 40; wait++) {
                velo_thread_sleep(3);
                n = velo_read_file("/VeloOS/System32/STDOUT.DAT", stdout_buf, sizeof(stdout_buf) - 1);
                if (n <= 0) {
                    n = velo_read_file("/STDOUT.DAT", stdout_buf, sizeof(stdout_buf) - 1);
                }
                if (n > 0) {
                    // Kurzer Sicherheits-Sleep, um den kompletten Puffer einzulesen
                    velo_thread_sleep(2);
                    int n2 = velo_read_file("/VeloOS/System32/STDOUT.DAT", stdout_buf, sizeof(stdout_buf) - 1);
                    if (n2 <= 0) n2 = velo_read_file("/STDOUT.DAT", stdout_buf, sizeof(stdout_buf) - 1);
                    if (n2 > n) n = n2;
                    break;
                }
            }

            if (n > 0) {
                stdout_buf[n] = '\0';
                term_print_raw(stdout_buf);
                if (stdout_buf[n - 1] != '\n') term_new_line();
            }

            velo_delete_file("/VeloOS/System32/STDOUT.DAT");
            velo_delete_file("/STDOUT.DAT");
            velo_delete_file("/VeloOS/System32/CMDLINE.DAT");
            velo_delete_file("/CMDLINE.DAT");
            status = 0;
        
        } else {
            char err[160];
            snprintf(err, sizeof(err), "\033[31msh: %s: Befehl nicht gefunden\033[0m", c);
            term_print(err);
            status = 127;
        }
    }

    if (g_redirect_mode > 0 && g_redirect_file[0]) {
        if (g_redirect_mode == 1) {
            velo_write_file(g_redirect_file, g_out_buf, (UINT32)g_out_len);
        } else if (g_redirect_mode == 2) {
            char exist_buf[32768];
            int exist_bytes = velo_read_file(g_redirect_file, exist_buf, sizeof(exist_buf) - g_out_len - 1);
            if (exist_bytes > 0) {
                memcpy(exist_buf + exist_bytes, g_out_buf, g_out_len);
                velo_write_file(g_redirect_file, exist_buf, (UINT32)(exist_bytes + g_out_len));
            } else {
                velo_write_file(g_redirect_file, g_out_buf, (UINT32)g_out_len);
            }
        }
    }

    g_redirect_mode = 0;
    g_last_exit_code = status;
    return status;
}

static void execute_pipeline(void) {
    if (g_input_len == 0) {
        char prompt_empty[160];
        snprintf(prompt_empty, sizeof(prompt_empty), "\033[1;32m%s@%s\033[0m:\033[1;34m%s\033[0m$ ", 
                 get_prompt_user(), get_prompt_host(), g_cwd);
        term_print(prompt_empty);
        return;
    }

    char echo_line[MAX_CMD_LEN + 128];
    snprintf(echo_line, sizeof(echo_line), "\033[1;32m%s@%s\033[0m:\033[1;34m%s\033[0m$ %s", 
             get_prompt_user(), get_prompt_host(), g_cwd, g_input_buffer);
    term_print(echo_line);

    if (g_hist_count < MAX_HISTORY) {
        safe_copy(g_history[g_hist_count++], g_input_buffer, MAX_CMD_LEN);
    } else {
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            safe_copy(g_history[i], g_history[i + 1], MAX_CMD_LEN);
        }
        safe_copy(g_history[MAX_HISTORY - 1], g_input_buffer, MAX_CMD_LEN);
    }
    g_hist_idx = g_hist_count;

    char line_copy[MAX_CMD_LEN];
    safe_copy(line_copy, g_input_buffer, sizeof(line_copy));

    char *cursor = line_copy;
    int should_run = 1;
    int last_status = 0;

    while (*cursor) {
        while (*cursor && isspace((unsigned char)*cursor)) cursor++;
        if (!*cursor) break;

        char subcmd[MAX_CMD_LEN] = "";
        int s = 0;
        int next_op = 0;
        char in_quote = 0;

        while (*cursor) {
            if (!in_quote && (*cursor == '\'' || *cursor == '"')) {
                in_quote = *cursor;
            } else if (in_quote && *cursor == in_quote) {
                in_quote = 0;
            } else if (!in_quote && *cursor == '\\' && *(cursor + 1)) {
                if (s < MAX_CMD_LEN - 2) {
                    subcmd[s++] = *cursor++;
                    subcmd[s++] = *cursor++;
                }
                continue;
            } else if (!in_quote) {
                if (*cursor == ';') {
                    next_op = 1;
                    cursor++;
                    break;
                } else if (cursor[0] == '&' && cursor[1] == '&') {
                    next_op = 2;
                    cursor += 2;
                    break;
                } else if (cursor[0] == '|' && cursor[1] == '|') {
                    next_op = 3;
                    cursor += 2;
                    break;
                }
            }

            if (s < MAX_CMD_LEN - 1) subcmd[s++] = *cursor;
            cursor++;
        }
        subcmd[s] = '\0';

        if (should_run) {
            last_status = execute_single_command(subcmd);
        }

        if (next_op == 1) {
            should_run = 1;
        } else if (next_op == 2) {
            if (!should_run || last_status != 0) should_run = 0;
            else should_run = 1;
        } else if (next_op == 3) {
            if (should_run && last_status == 0) should_run = 0;
            else should_run = 1;
        }
    }

    g_input_buffer[0] = '\0';
    g_input_len = 0;
    g_cursor_pos = 0;
    g_scroll_offset = 0;
}

static void handle_tab_completion(void) {
    if (g_input_len == 0) return;

    int last_space = -1;
    for (int i = 0; i < g_input_len; i++) {
        if (g_input_buffer[i] == ' ') last_space = i;
    }

    const char *prefix = (last_space >= 0) ? &g_input_buffer[last_space + 1] : g_input_buffer;
    size_t prefix_len = strlen(prefix);
    if (prefix_len == 0) return;

    VeloDirEntry entries[64];
    int count = velo_list_dir(g_cwd, entries, 64);
    if (count <= 0) return;

    for (int i = 0; i < count; i++) {
        if (strncmp(entries[i].name, prefix, prefix_len) == 0) {
            const char *rest = entries[i].name + prefix_len;
            while (*rest && g_input_len < MAX_CMD_LEN - 1) {
                g_input_buffer[g_input_len++] = *rest++;
            }
            g_input_buffer[g_input_len] = '\0';
            g_cursor_pos = g_input_len;
            break;
        }
    }
}

static void render_terminal(velo_window_t win) {
    velo_window_clear(win);
    velo_window_draw_rect_color(win, 0, 0, g_win_w, g_win_h, 0x000F172A);

    int visible_cols = (g_win_w - SCROLLBAR_WIDTH - 20) / CHAR_WIDTH;
    if (visible_cols < 20) visible_cols = 20;

    char prompt[160];
    snprintf(prompt, sizeof(prompt), "%s@%s:%s$ ", get_prompt_user(), get_prompt_host(), g_cwd);
    int prompt_len = (int)strlen(prompt);

    int input_rows = (prompt_len + g_input_len) / visible_cols + 1;

    int max_visible_lines = (g_win_h - 16) / LINE_HEIGHT - input_rows;
    if (max_visible_lines < 1) max_visible_lines = 1;

    int max_scroll = g_line_count - max_visible_lines;
    if (max_scroll < 0) max_scroll = 0;

    int end_line = g_line_count - g_scroll_offset;
    if (end_line > g_line_count) end_line = g_line_count;
    if (end_line < max_visible_lines) end_line = (g_line_count < max_visible_lines) ? g_line_count : max_visible_lines;

    int start_line = end_line - max_visible_lines;
    if (start_line < 0) start_line = 0;

    int cur_y = 6;
    for (int l = start_line; l < end_line; l++) {
        int len = g_line_lens[l];
        if (len > 0) {
            int cur_x = 8;
            char chunk[MAX_COLS];
            int chunk_len = 0;
            UINT32 chunk_col = g_term[l][0].fg;

            for (int c = 0; c < len; c++) {
                if (g_term[l][c].fg != chunk_col) {
                    chunk[chunk_len] = '\0';
                    velo_window_draw_text_colored(win, chunk, cur_x, cur_y, chunk_col);
                    cur_x += chunk_len * CHAR_WIDTH;
                    chunk_len = 0;
                    chunk_col = g_term[l][c].fg;
                }
                chunk[chunk_len++] = g_term[l][c].ch;
            }
            if (chunk_len > 0) {
                chunk[chunk_len] = '\0';
                velo_window_draw_text_colored(win, chunk, cur_x, cur_y, chunk_col);
            }
        }
        cur_y += LINE_HEIGHT;
    }

    int input_start_y = cur_y;
    int cur_col = 0;
    int cur_row = 0;

    for (int i = 0; i < prompt_len; i++) {
        char ch[2] = { prompt[i], '\0' };
        int px = 8 + cur_col * CHAR_WIDTH;
        int py = input_start_y + cur_row * LINE_HEIGHT;
        velo_window_draw_text_colored(win, ch, px, py, COLOR_GREEN);
        cur_col++;
        if (cur_col >= visible_cols) {
            cur_col = 0;
            cur_row++;
        }
    }

    int cursor_px = 8 + cur_col * CHAR_WIDTH;
    int cursor_py = input_start_y + cur_row * LINE_HEIGHT;

    for (int i = 0; i < g_input_len; i++) {
        int px = 8 + cur_col * CHAR_WIDTH;
        int py = input_start_y + cur_row * LINE_HEIGHT;
        if (i == g_cursor_pos) {
            cursor_px = px;
            cursor_py = py;
        }

        char ch[2] = { g_input_buffer[i], '\0' };
        velo_window_draw_text_colored(win, ch, px, py, COLOR_WHITE);
        cur_col++;
        if (cur_col >= visible_cols) {
            cur_col = 0;
            cur_row++;
        }
    }

    if (g_cursor_pos == g_input_len) {
        cursor_px = 8 + cur_col * CHAR_WIDTH;
        cursor_py = input_start_y + cur_row * LINE_HEIGHT;
    }

    velo_window_draw_rect_color(win, cursor_px, cursor_py, CHAR_WIDTH, 14, COLOR_CYAN);

    if (g_cursor_pos < g_input_len) {
        char ch_buf[2] = { g_input_buffer[g_cursor_pos], '\0' };
        UINT32 comp_color = 0x00C74207;
        velo_window_draw_text_colored(win, ch_buf, cursor_px, cursor_py, comp_color);
    }

    int sb_x = g_win_w - SCROLLBAR_WIDTH;
    velo_window_draw_rect_color(win, sb_x, 0, SCROLLBAR_WIDTH, g_win_h, 0x001E293B);

    if (g_line_count > max_visible_lines) {
        float view_ratio = (float)max_visible_lines / (float)g_line_count;
        int thumb_h = (int)(view_ratio * (float)g_win_h);
        if (thumb_h < 24) thumb_h = 24;

        float pos_ratio = (float)(max_scroll - g_scroll_offset) / (float)max_scroll;
        int thumb_y = (int)(pos_ratio * (float)(g_win_h - thumb_h));
        if (thumb_y < 0) thumb_y = 0;
        if (thumb_y + thumb_h > g_win_h) thumb_y = g_win_h - thumb_h;

        UINT32 thumb_col = (g_scroll_offset > 0) ? 0x0038BDF8 : 0x00475569;
        velo_window_draw_rect_color(win, sb_x + 2, thumb_y, SCROLLBAR_WIDTH - 4, thumb_h, thumb_col);
    }

    if (g_scroll_offset > 0) {
        char scroll_hint[64];
        snprintf(scroll_hint, sizeof(scroll_hint), "[Verlauf: +%d Zeilen]", g_scroll_offset);
        velo_window_draw_text_colored(win, scroll_hint, g_win_w - 240, 6, COLOR_YELLOW);
    }

    velo_window_redraw();
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    VeloSysInfo info;
    if (velo_get_sysinfo(&info) == 0) {
        env_set("USER", info.user_name[0] ? info.user_name : "user");
        env_set("HOSTNAME", info.pc_name[0] ? info.pc_name : "velo-pc");
    } else {
        env_set("USER", "user");
        env_set("HOSTNAME", "velo-pc");
    }

    safe_copy(g_cwd, "C:/Users", sizeof(g_cwd));
    env_set("HOME", "C:/Users");
    env_set("OLDPWD", "C:/Users");
    env_set("PWD", "C:/Users");
    env_set("SHELL", "/bin/sh");
    env_set("PATH", "C:/BIN;C:/bin;/bin;C:/Users;.");

    term_print("\033[1;36mVeloOS POSIX/Bash Shell [Version 11.0]\033[0m");

    velo_window_t win = velo_window_create("Terminal", g_win_w, g_win_h);
    if (win < 0) return 0;

    g_win_w = velo_window_get_width(win);
    g_win_h = velo_window_get_height(win);

    render_terminal(win);

    velo_event_t ev;
    while (1) {
        int st = velo_poll_event(win, &ev);
        if (st == -1) break;
        if (st == 0) {
            velo_thread_sleep(1);
            continue;
        }

        if (st == 1 && ev.type == VELO_EV_RESIZE) {
            g_win_w = ev.x;
            g_win_h = ev.y;
            render_terminal(win);
            continue;
        }

        if (st == 1 && ev.type == VELO_EV_CLICK && ev.x >= (g_win_w - SCROLLBAR_WIDTH - 6)) {
            int visible_cols = (g_win_w - SCROLLBAR_WIDTH - 20) / CHAR_WIDTH;
            if (visible_cols < 20) visible_cols = 20;
            char pr[160];
            snprintf(pr, sizeof(pr), "%s@%s:%s$ ", get_prompt_user(), get_prompt_host(), g_cwd);
            int in_rows = ((int)strlen(pr) + g_input_len) / visible_cols + 1;
            int max_visible_lines = (g_win_h - 16) / LINE_HEIGHT - in_rows;
            int max_scroll = g_line_count - max_visible_lines;
            if (max_scroll > 0) {
                float click_ratio = (float)ev.y / (float)g_win_h;
                g_scroll_offset = (int)((1.0f - click_ratio) * (float)max_scroll);
                if (g_scroll_offset < 0) g_scroll_offset = 0;
                if (g_scroll_offset > max_scroll) g_scroll_offset = max_scroll;
                render_terminal(win);
                continue;
            }
        }

        if (st == 1 && ev.type == VELO_EV_SCROLL) {
            int visible_cols = (g_win_w - SCROLLBAR_WIDTH - 20) / CHAR_WIDTH;
            if (visible_cols < 20) visible_cols = 20;
            char pr[160];
            snprintf(pr, sizeof(pr), "%s@%s:%s$ ", get_prompt_user(), get_prompt_host(), g_cwd);
            int in_rows = ((int)strlen(pr) + g_input_len) / visible_cols + 1;
            int max_visible_lines = (g_win_h - 16) / LINE_HEIGHT - in_rows;
            int max_scroll = g_line_count - max_visible_lines;
            if (max_scroll > 0) {
                if (ev.scroll_y > 0) g_scroll_offset += 4;
                else if (ev.scroll_y < 0) g_scroll_offset -= 4;
                if (g_scroll_offset < 0) g_scroll_offset = 0;
                if (g_scroll_offset > max_scroll) g_scroll_offset = max_scroll;
                render_terminal(win);
                continue;
            }
        }

        if (st == 1 && ev.type == VELO_EV_KEY) {
            if (ev.key == '\n') {
                execute_pipeline();
            } else if (ev.key == '\t') {
                handle_tab_completion();
            } else if (ev.key == (char)0x82 /* KEY_UP */) {
                if (g_hist_count > 0 && g_hist_idx > 0) {
                    g_hist_idx--;
                    safe_copy(g_input_buffer, g_history[g_hist_idx], sizeof(g_input_buffer));
                    g_input_len = (int)strlen(g_input_buffer);
                    g_cursor_pos = g_input_len;
                }
            } else if (ev.key == (char)0x83 /* KEY_DOWN */) {
                if (g_hist_idx < g_hist_count - 1) {
                    g_hist_idx++;
                    safe_copy(g_input_buffer, g_history[g_hist_idx], sizeof(g_input_buffer));
                    g_input_len = (int)strlen(g_input_buffer);
                    g_cursor_pos = g_input_len;
                } else {
                    g_hist_idx = g_hist_count;
                    g_input_buffer[0] = '\0';
                    g_input_len = 0;
                    g_cursor_pos = 0;
                }
            } else if (ev.key == (char)0x84 /* KEY_LEFT */) {
                if (g_cursor_pos > 0) g_cursor_pos--;
            } else if (ev.key == (char)0x85 /* KEY_RIGHT */) {
                if (g_cursor_pos < g_input_len) g_cursor_pos++;
            } else if (ev.key == (char)0x86 /* KEY_HOME */) {
                g_cursor_pos = 0;
            } else if (ev.key == (char)0x87 /* KEY_END */) {
                g_cursor_pos = g_input_len;
            } else if (ev.key == '\b') {
                if (g_cursor_pos > 0) {
                    for (int i = g_cursor_pos - 1; i < g_input_len; i++) {
                        g_input_buffer[i] = g_input_buffer[i + 1];
                    }
                    g_input_len--;
                    g_cursor_pos--;
                }
            } else if ((unsigned char)ev.key == 0x88 || (unsigned char)ev.key == 0x7F /* KEY_DELETE */) {
                if (g_cursor_pos < g_input_len) {
                    for (int i = g_cursor_pos; i < g_input_len; i++) {
                        g_input_buffer[i] = g_input_buffer[i + 1];
                    }
                    g_input_len--;
                }
            } else if ((unsigned char)ev.key >= 32 && (unsigned char)ev.key < 127 && g_input_len < MAX_CMD_LEN - 1) {
                for (int i = g_input_len; i >= g_cursor_pos; i--) {
                    g_input_buffer[i + 1] = g_input_buffer[i];
                }
                g_input_buffer[g_cursor_pos] = ev.key;
                g_input_len++;
                g_cursor_pos++;
            }

            render_terminal(win);
        }
    }

    return 0;
}
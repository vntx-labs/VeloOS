#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <velo/window.h>
#include <velo/syscall.h>
#include <velo/net.h>

#define MAX_HISTORY     32
#define MAX_CMD_LEN     256
#define MAX_LINES       64
#define MAX_LINE_CHARS  128
#define LINE_HEIGHT     18

static char g_terminal_lines[MAX_LINES][MAX_LINE_CHARS];
static int  g_line_count = 0;

static char g_input_buffer[MAX_CMD_LEN] = "";
static int  g_input_len = 0;
static int  g_cursor_pos = 0;

static char g_history[MAX_HISTORY][MAX_CMD_LEN];
static int  g_hist_count = 0;
static int  g_hist_idx = -1;

static char g_cwd[128] = "C:/Users/Desktop";

static int g_win_w = 640;
static int g_win_h = 420;

// Puffer für Output-Redirection (> und >>)
static char g_out_buf[16384] = "";
static int  g_out_len = 0;
static int  g_redirect_mode = 0; // 0 = Screen, 1 = Overwrite (>), 2 = Append (>>)
static char g_redirect_file[128] = "";

typedef struct {
    int major;
    const char *codename;
    const char *description;
} VeloVersionInfo;

static const VeloVersionInfo g_velo_codenames[10] = {
    {1,  " Genesis"},
    {2,  " Neon"},
    {3,  " Argon"},
    {4,  " Krypton"},
    {5,  " Radon"},
    {6,  " Cobalt"},
    {7,  " Titanium"},
    {8,  " Obsidian"},
    {9,  " Quantum"},
    {10, "Xenon"}
};

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

static int parse_two_args(const char *input, char *arg1, size_t max1, char *arg2, size_t max2) {
    if (!input || !arg1 || !arg2 || max1 == 0 || max2 == 0) return 0;
    
    arg1[0] = '\0';
    arg2[0] = '\0';

    const char *p = input;
    while (*p && isspace(*p)) p++;
    if (!*p) return 0;

    size_t i1 = 0;
    if (*p == '"' || *p == '\'') {
        char quote = *p++;
        while (*p && *p != quote && i1 < max1 - 1) arg1[i1++] = *p++;
        if (*p == quote) p++;
    } else {
        while (*p && !isspace(*p) && i1 < max1 - 1) arg1[i1++] = *p++;
    }
    arg1[i1] = '\0';

    while (*p && isspace(*p)) p++;
    if (!*p) return 1;

    size_t i2 = 0;
    if (*p == '"' || *p == '\'') {
        char quote = *p++;
        while (*p && *p != quote && i2 < max2 - 1) arg2[i2++] = *p++;
        if (*p == quote) p++;
    } else {
        while (*p && !isspace(*p) && i2 < max2 - 1) arg2[i2++] = *p++;
    }
    arg2[i2] = '\0';

    return 2;
}

static void term_add_single_line(const char *line) {
    if (!line) return;

    if (g_line_count >= MAX_LINES - 1) {
        for (int i = 0; i < MAX_LINES - 1; i++) {
            memcpy(g_terminal_lines[i], g_terminal_lines[i + 1], MAX_LINE_CHARS);
        }
        g_line_count = MAX_LINES - 2;
    }

    safe_copy(g_terminal_lines[g_line_count], line, MAX_LINE_CHARS);
    g_line_count++;
}

static void term_print(const char *text) {
    if (!text) return;

    if (g_redirect_mode > 0) {
        size_t t_len = strlen(text);
        if ((size_t)g_out_len + t_len + 2 < sizeof(g_out_buf)) {
            memcpy(g_out_buf + g_out_len, text, t_len);
            g_out_len += (int)t_len;
            g_out_buf[g_out_len++] = '\n';
            g_out_buf[g_out_len] = '\0';
        }
        return;
    }

    char buf[MAX_LINE_CHARS];
    int pos = 0;

    for (int i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\n') {
            buf[pos] = '\0';
            term_add_single_line(buf);
            pos = 0;
        } else if (pos < MAX_LINE_CHARS - 1) {
            buf[pos++] = text[i];
        } else {
            buf[pos] = '\0';
            term_add_single_line(buf);
            pos = 0;
            buf[pos++] = text[i];
        }
    }

    if (pos > 0 || text[0] == '\0') {
        buf[pos] = '\0';
        term_add_single_line(buf);
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

static int cmd_dir(const char *arg) {
    char target_path[128];
    if (arg && arg[0]) {
        build_full_path(arg, target_path, sizeof(target_path));
    } else {
        safe_copy(target_path, g_cwd, sizeof(target_path));
    }

    VeloDirEntry entries[128];
    int count = velo_list_dir(target_path, entries, 128);

    char header[128];
    snprintf(header, sizeof(header), " Verzeichnis von %s\n", target_path);
    term_print(header);

    if (count < 0) {
        term_print("  [-] Verzeichnis konnte nicht geoeffnet werden.");
        return 0;
    }

    int files = 0, dirs = 0;
    for (int i = 0; i < count; i++) {
        char line[128];
        if (entries[i].is_dir) {
            snprintf(line, sizeof(line), "  <DIR>          %s", entries[i].name);
            dirs++;
        } else {
            snprintf(line, sizeof(line), "        %7u  %s", (unsigned int)entries[i].size, entries[i].name);
            files++;
        }
        term_print(line);
    }

    char footer[128];
    snprintf(footer, sizeof(footer), "\n  %d Datei(en), %d Verzeichnis(se)", files, dirs);
    term_print(footer);
    return 1;
}

static int cmd_cd(const char *arg) {
    if (!arg || !arg[0]) {
        term_print(g_cwd);
        return 1;
    }

    if (strcmp(arg, "..") == 0) {
        char *last_slash = strrchr(g_cwd, '/');
        if (!last_slash) last_slash = strrchr(g_cwd, '\\');
        if (last_slash && last_slash != g_cwd) {
            if (last_slash - g_cwd == 2 && g_cwd[1] == ':') {
                *(last_slash + 1) = '\0';
            } else {
                *last_slash = '\0';
            }
        } else {
            safe_copy(g_cwd, "C:/", sizeof(g_cwd));
        }
        return 1;
    }

    if (strcmp(arg, "/") == 0 || strcmp(arg, "\\") == 0 || strcmp(arg, "C:") == 0 || strcmp(arg, "C:/") == 0) {
        safe_copy(g_cwd, "C:/", sizeof(g_cwd));
        return 1;
    }

    char target[128];
    build_full_path(arg, target, sizeof(target));

    VeloDirEntry test_entries[2];
    int res = velo_list_dir(target, test_entries, 2);
    if (res >= 0) {
        safe_copy(g_cwd, target, sizeof(g_cwd));
        return 1;
    } else {
        term_print("[-] Das System kann den angegebenen Pfad nicht finden.");
        return 0;
    }
}

static int cmd_cat(const char *arg) {
    if (!arg || !arg[0]) {
        term_print("Syntax: type <dateiname> oder cat <dateiname>");
        return 0;
    }

    char full_p[128];
    build_full_path(arg, full_p, sizeof(full_p));

    char file_buf[4096];
    int bytes = velo_read_file(full_p, file_buf, sizeof(file_buf) - 1);
    if (bytes >= 0) {
        file_buf[bytes] = '\0';
        term_print(file_buf);
        return 1;
    } else {
        term_print("[-] Datei nicht gefunden oder Lesefehler.");
        return 0;
    }
}

static int cmd_echo(const char *arg) {
    if (!arg) {
        term_print("");
        return 1;
    }

    char clean[256];
    size_t len = strlen(arg);

    if (len >= 2 && ((arg[0] == '"' && arg[len - 1] == '"') || (arg[0] == '\'' && arg[len - 1] == '\''))) {
        safe_copy(clean, arg + 1, len);
        clean[len - 2] = '\0';
        term_print(clean);
    } else {
        term_print(arg);
    }
    return 1;
}

// Befehl: Codenames-Liste und Abfrage (codes [version])
static int cmd_codes(const char *arg) {
    if (!arg || !arg[0]) {
        term_print("=== Offizielle VeloOS Versions- & Codename-Tabelle ===");
        term_print("Version    Codename");
        term_print("-----------------------");

        for (int i = 0; i < 10; i++) {
            char line[128];
            snprintf(line, sizeof(line), "V%-9d %-12s %s", 
                     g_velo_codenames[i].major, 
                     g_velo_codenames[i].codename 
            );
            term_print(line);
        }
        term_print("-----------------------");
        term_print("Hinweis: Nur die Hauptversionsnummer bestimmt den Codename.");
        return 1;
    }

    // Bestimme die Versionsnummer vor dem ersten Punkt
    const char *p = arg;
    while (*p && (*p == 'v' || *p == 'V' || isspace(*p))) p++;

    int major_num = 0;
    while (*p && isdigit(*p)) {
        major_num = major_num * 10 + (*p - '0');
        p++;
    }

    if (major_num >= 1 && major_num <= 10) {
        char line[128];
        snprintf(line, sizeof(line), "[+] VeloOS V%d.x.x -> Codename: %s (%s)",
                 major_num,
                 g_velo_codenames[major_num - 1].codename,
                 g_velo_codenames[major_num - 1].description);
        term_print(line);
        return 1;
    } else {
        char err[128];
        snprintf(err, sizeof(err), "[-] Ungueltige Version '%s'. Gueltig sind V1 bis V10.", arg);
        term_print(err);
        return 0;
    }
}

static int execute_single_command(char *cmd_line) {
    while (*cmd_line && isspace(*cmd_line)) cmd_line++;
    if (!*cmd_line) return 1;

    g_redirect_mode = 0;
    g_redirect_file[0] = '\0';
    g_out_len = 0;
    g_out_buf[0] = '\0';

    char *redir_append = strstr(cmd_line, ">>");
    char *redir_trunc = strchr(cmd_line, '>');

    if (redir_append) {
        g_redirect_mode = 2;
        *redir_append = '\0';
        char *target = redir_append + 2;
        while (*target && isspace(*target)) target++;
        build_full_path(target, g_redirect_file, sizeof(g_redirect_file));
    } else if (redir_trunc) {
        g_redirect_mode = 1;
        *redir_trunc = '\0';
        char *target = redir_trunc + 1;
        while (*target && isspace(*target)) target++;
        build_full_path(target, g_redirect_file, sizeof(g_redirect_file));
    }

    char cmd[32] = "";
    char arg[256] = "";

    int p = 0;
    while (cmd_line[p] && !isspace(cmd_line[p]) && p < 31) {
        cmd[p] = cmd_line[p];
        p++;
    }
    cmd[p] = '\0';

    while (cmd_line[p] && isspace(cmd_line[p])) p++;
    safe_copy(arg, &cmd_line[p], sizeof(arg));

    size_t arg_l = strlen(arg);
    while (arg_l > 0 && isspace(arg[arg_l - 1])) {
        arg[--arg_l] = '\0';
    }

    int success = 1;

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        term_print("Verfuegbare Befehle:");
        term_print("  help / ?              - Zeigt diese Hilfe an");
        term_print("  codes [ver]           - Zeigt alle VeloOS Codenames (V1-V10)");
        term_print("  dir  / ls             - Listet Verzeichnisinhalte auf");
        term_print("  cd <pfad>             - Wechselt das Arbeitsverzeichnis");
        term_print("  pwd                   - Zeigt den aktuellen Pfad an");
        term_print("  type / cat <datei>    - Gibt Datei-Inhalt aus");
        term_print("  echo <text> [> datei] - Gibt Text aus / leitet in Datei um");
        term_print("  touch <datei>         - Erstellt eine neue leere Datei");
        term_print("  cp   / copy <q> <z>   - Kopiert eine Datei");
        term_print("  mv   / move <q> <z>   - Verschiebt eine Datei");
        term_print("  ren  / rename <a <n>  - Benennt eine Datei um");
        term_print("  md   / mkdir <ordner> - Erstellt einen neuen Ordner");
        term_print("  del  / rm <datei>     - Loescht eine Datei");
        term_print("  whoami / hostname     - Zeigt Benutzer- / PC-Informationen");
        term_print("  uname                 - Gibt OS-Kernel-Version aus");
        term_print("  clear / cls           - Loescht das Terminal");
        term_print("  sysinfo               - Detaillierte Hardware-Specs");
        term_print("  explorer / notepad    - Startet Systemanwendungen");
        term_print("  exit                  - Schliesst das Terminal");
    }
    else if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0) {
        g_line_count = 0;
        for (int i = 0; i < MAX_LINES; i++) g_terminal_lines[i][0] = '\0';
    }
    else if (strcmp(cmd, "codes") == 0) {
        success = cmd_codes(arg);
    }
    else if (strcmp(cmd, "dir") == 0 || strcmp(cmd, "ls") == 0) {
        success = cmd_dir(arg);
    }
    else if (strcmp(cmd, "cd") == 0) {
        success = cmd_cd(arg);
    }
    else if (strcmp(cmd, "pwd") == 0) {
        term_print(g_cwd);
    }
    else if (strcmp(cmd, "type") == 0 || strcmp(cmd, "cat") == 0) {
        success = cmd_cat(arg);
    }
    else if (strcmp(cmd, "echo") == 0) {
        success = cmd_echo(arg);
    }
    else if (strcmp(cmd, "touch") == 0) {
        if (!arg[0]) {
            term_print("Syntax: touch <dateiname>");
            success = 0;
        } else {
            char target[128];
            build_full_path(arg, target, sizeof(target));
            if (velo_create_file(target)) term_print("[+] Datei erstellt.");
            else { term_print("[-] Fehler beim Erstellen der Datei."); success = 0; }
        }
    }
    else if (strcmp(cmd, "copy") == 0 || strcmp(cmd, "cp") == 0) {
        char src[128] = "", dst[128] = "";
        if (parse_two_args(arg, src, sizeof(src), dst, sizeof(dst)) == 2) {
            char full_src[128], full_dst[128];
            build_full_path(src, full_src, sizeof(full_src));
            build_full_path(dst, full_dst, sizeof(full_dst));
            if (velo_copy_file(full_src, full_dst)) term_print("[+] 1 Datei(en) kopiert.");
            else { term_print("[-] Fehler beim Kopieren der Datei."); success = 0; }
        } else {
            term_print("Syntax: cp <quelle> <ziel>");
            success = 0;
        }
    }
    else if (strcmp(cmd, "move") == 0 || strcmp(cmd, "mv") == 0) {
        char src[128] = "", dst[128] = "";
        if (parse_two_args(arg, src, sizeof(src), dst, sizeof(dst)) == 2) {
            char full_src[128], full_dst[128];
            build_full_path(src, full_src, sizeof(full_src));
            build_full_path(dst, full_dst, sizeof(full_dst));
            if (velo_move_file(full_src, full_dst)) term_print("[+] 1 Datei(en) verschoben.");
            else { term_print("[-] Fehler beim Verschieben der Datei."); success = 0; }
        } else {
            term_print("Syntax: mv <quelle> <ziel>");
            success = 0;
        }
    }
    else if (strcmp(cmd, "rename") == 0 || strcmp(cmd, "ren") == 0) {
        char old_n[128] = "", new_n[64] = "";
        if (parse_two_args(arg, old_n, sizeof(old_n), new_n, sizeof(new_n)) == 2) {
            char full_old[128];
            build_full_path(old_n, full_old, sizeof(full_old));
            if (velo_rename_file(full_old, new_n)) term_print("[+] Datei umbenannt.");
            else { term_print("[-] Fehler beim Umbenennen."); success = 0; }
        } else {
            term_print("Syntax: ren <alter_name> <neuer_name>");
            success = 0;
        }
    }
    else if (strcmp(cmd, "mkdir") == 0 || strcmp(cmd, "md") == 0) {
        if (!arg[0]) {
            term_print("Syntax: mkdir <ordnername>");
            success = 0;
        } else {
            char target[128];
            build_full_path(arg, target, sizeof(target));
            if (velo_mkdir(target)) term_print("[+] Ordner erfolgreich erstellt.");
            else { term_print("[-] Fehler beim Erstellen des Ordners."); success = 0; }
        }
    }
    else if (strcmp(cmd, "del") == 0 || strcmp(cmd, "rm") == 0) {
        if (!arg[0]) {
            term_print("Syntax: del <dateiname>");
            success = 0;
        } else {
            char target[128];
            build_full_path(arg, target, sizeof(target));
            if (velo_delete_file(target)) term_print("[+] Datei geloescht.");
            else { term_print("[-] Datei nicht gefunden oder geschuetzt."); success = 0; }
        }
    }
    else if (strcmp(cmd, "whoami") == 0) {
        VeloSysInfo info;
        if (velo_get_sysinfo(&info) == 0) term_print(info.user_name);
        else term_print("Benutzer");
    }
    else if (strcmp(cmd, "hostname") == 0) {
        VeloSysInfo info;
        if (velo_get_sysinfo(&info) == 0) term_print(info.pc_name);
        else term_print("Velo-PC");
    }
    else if (strcmp(cmd, "uname") == 0) {
        term_print("VeloOS Xenon (Version 10.1.0 x86_64)");
    }
    else if (strcmp(cmd, "sysinfo") == 0) {
        VeloSysInfo info;
        if (velo_get_sysinfo(&info) == 0) {
            char cpu_b[128], ram_b[64], pc_b[64];
            snprintf(pc_b,  sizeof(pc_b),  "  PC / User:  %s / %s", info.pc_name, info.user_name);
            snprintf(cpu_b, sizeof(cpu_b), "  Prozessor:  %s", info.cpu_brand[0] ? info.cpu_brand : "x86_64");
            snprintf(ram_b, sizeof(ram_b), "  Speicher:   %u MB RAM", (unsigned int)info.total_ram_mb);
            term_print("Systeminformationen:");
            term_print(pc_b);
            term_print(cpu_b);
            term_print(ram_b);
        }
    }
    else if (strcmp(cmd, "explorer") == 0 || strcmp(cmd, "EXPLORER") == 0) {
        velo_exec("EXPLORER.BIN");
    }
    else if (strcmp(cmd, "notepad") == 0 || strcmp(cmd, "NOTEPAD") == 0) {
        velo_exec("NOTEPAD.BIN");
    }
    else if (strcmp(cmd, "sh") == 0 || strcmp(cmd, "SH") == 0) {
        velo_exec("SH.BIN");
    }
    else if (strcmp(cmd, "exit") == 0) {
        exit(0);
    }
    else {
        char full_exec[128];
        build_full_path(cmd, full_exec, sizeof(full_exec));

        if (velo_exec(full_exec) <= 0 && velo_exec(cmd) <= 0) {
            char err[160];
            snprintf(err, sizeof(err), "'%s' ist entweder falsch geschrieben oder konnte nicht gefunden werden.", cmd);
            term_print(err);
            success = 0;
        }
    }

    if (g_redirect_mode > 0 && g_redirect_file[0]) {
        if (g_redirect_mode == 1) {
            velo_write_file(g_redirect_file, g_out_buf, (UINT32)g_out_len);
        } else if (g_redirect_mode == 2) {
            char exist_buf[16384];
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
    return success;
}

static void execute_pipeline(void) {
    if (g_input_len == 0) {
        char prompt_empty[160];
        snprintf(prompt_empty, sizeof(prompt_empty), "%s> ", g_cwd);
        term_print(prompt_empty);
        return;
    }

    char echo_line[256];
    snprintf(echo_line, sizeof(echo_line), "%s> %s", g_cwd, g_input_buffer);
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
    int last_status = 1;

    while (*cursor) {
        while (*cursor && isspace(*cursor)) cursor++;
        if (!*cursor) break;

        char current_subcmd[MAX_CMD_LEN] = "";
        int sub_len = 0;
        int next_op = 0; // 0 = Ende, 1 = ;, 2 = &&, 3 = ||

        while (*cursor) {
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
            if (sub_len < MAX_CMD_LEN - 1) current_subcmd[sub_len++] = *cursor;
            cursor++;
        }
        current_subcmd[sub_len] = '\0';

        last_status = execute_single_command(current_subcmd);

        if (next_op == 2 && !last_status) {
            while (*cursor && *cursor != ';' && !(cursor[0] == '|' && cursor[1] == '|')) cursor++;
            if (*cursor == ';') cursor++;
            else if (cursor[0] == '|' && cursor[1] == '|') cursor += 2;
        } else if (next_op == 3 && last_status) {
            while (*cursor && *cursor != ';' && !(cursor[0] == '&' && cursor[1] == '&')) cursor++;
            if (*cursor == ';') cursor++;
            else if (cursor[0] == '&' && cursor[1] == '&') cursor += 2;
        }
    }

    g_input_buffer[0] = '\0';
    g_input_len = 0;
    g_cursor_pos = 0;
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

    velo_window_draw_rect_color(win, 0, 0, g_win_w, g_win_h, 0x000A1118);

    int max_visible_lines = (g_win_h - 28) / LINE_HEIGHT;
    if (max_visible_lines < 1) max_visible_lines = 1;

    int start_line = (g_line_count > max_visible_lines) ? (g_line_count - max_visible_lines) : 0;

    int cur_y = 6;
    for (int i = start_line; i < g_line_count; i++) {
        if (g_terminal_lines[i][0] != '\0') {
            velo_window_draw_text_colored(win, g_terminal_lines[i], 8, cur_y, 0x00E2E8F0);
            cur_y += LINE_HEIGHT;
        }
    }

    char prompt[160];
    snprintf(prompt, sizeof(prompt), "%s> ", g_cwd);
    velo_window_draw_text_colored(win, prompt, 8, cur_y, 0x0038BDF8);

    int prompt_pixel_len = (int)strlen(prompt) * 8;
    velo_window_draw_text_colored(win, g_input_buffer, 8 + prompt_pixel_len, cur_y, 0x00FFFFFF);

    int cur_x = 8 + prompt_pixel_len + g_cursor_pos * 8;
    velo_window_draw_rect_color(win, cur_x, cur_y, 8, 14, 0x0038BDF8);

    velo_window_redraw();
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    term_print("VeloOS Shell [Version 10.1.0 Xenon]");
    term_print("Copyright (C) 2026 vntx-labs. Alle Rechte vorbehalten.\n");
    term_print("Geben Sie 'help' oder 'codes' ein, um Befehle und Codenames zu sehen.\n");

    velo_window_t win = velo_window_create("Eingabeaufforderung", g_win_w, g_win_h);
    if (win < 0) return 0;

    g_win_w = velo_window_get_width(win);
    g_win_h = velo_window_get_height(win);

    render_terminal(win);

    velo_event_t ev;
    while (1) {
        int st = velo_poll_event(win, &ev);
        if (st == -1) break;
        if (st == 0) { velo_thread_sleep(1); continue; }

        if (st == 1 && ev.type == VELO_EV_RESIZE) {
            g_win_w = ev.x;
            g_win_h = ev.y;
            render_terminal(win);
            continue;
        }

        if (st == 1 && ev.type == VELO_EV_KEY) {
            if (ev.key == '\n') {
                execute_pipeline();
            }
            else if (ev.key == '\t') {
                handle_tab_completion();
            }
            else if (ev.key == KEY_UP) {
                if (g_hist_count > 0 && g_hist_idx > 0) {
                    g_hist_idx--;
                    safe_copy(g_input_buffer, g_history[g_hist_idx], sizeof(g_input_buffer));
                    g_input_len = (int)strlen(g_input_buffer);
                    g_cursor_pos = g_input_len;
                }
            }
            else if (ev.key == KEY_DOWN) {
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
            }
            else if (ev.key == KEY_LEFT) {
                if (g_cursor_pos > 0) g_cursor_pos--;
            }
            else if (ev.key == KEY_RIGHT) {
                if (g_cursor_pos < g_input_len) g_cursor_pos++;
            }
            else if (ev.key == KEY_HOME) {
                g_cursor_pos = 0;
            }
            else if (ev.key == KEY_END) {
                g_cursor_pos = g_input_len;
            }
            else if (ev.key == '\b') {
                if (g_cursor_pos > 0) {
                    for (int i = g_cursor_pos - 1; i < g_input_len; i++) {
                        g_input_buffer[i] = g_input_buffer[i + 1];
                    }
                    g_input_len--;
                    g_cursor_pos--;
                }
            }
            else if ((unsigned char)ev.key == 0x88 || (unsigned char)ev.key == 0x7F) {
                if (g_cursor_pos < g_input_len) {
                    for (int i = g_cursor_pos; i < g_input_len; i++) {
                        g_input_buffer[i] = g_input_buffer[i + 1];
                    }
                    g_input_len--;
                }
            }
            else if ((unsigned char)ev.key >= 32 && g_input_len < MAX_CMD_LEN - 1) {
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
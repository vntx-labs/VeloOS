#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <velo/syscall.h>

static int str_contains_icase(const char *h, const char *n) {
    if (!n || !n[0]) return 1;
    for (int i = 0; h[i]; i++) {
        int m = 1;
        for (int k = 0; n[k]; k++) {
            if (!h[i+k] || tolower((unsigned char)h[i+k]) != tolower((unsigned char)n[k])) {
                m = 0; break;
            }
        }
        if (m) return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    int opt_i = 0, opt_v = 0, opt_c = 0, opt_n = 0, opt_l = 0;
    int arg_idx = 1;

    for (; arg_idx < argc; arg_idx++) {
        if (argv[arg_idx][0] == '-' && argv[arg_idx][1]) {
            for (int k = 1; argv[arg_idx][k]; k++) {
                if (argv[arg_idx][k] == 'i') opt_i = 1;
                else if (argv[arg_idx][k] == 'v') opt_v = 1;
                else if (argv[arg_idx][k] == 'c') opt_c = 1;
                else if (argv[arg_idx][k] == 'n') opt_n = 1;
                else if (argv[arg_idx][k] == 'l') opt_l = 1;
            }
        } else {
            break;
        }
    }

    if (argc - arg_idx < 2) {
        printf("grep: Syntax: grep [-i] [-v] [-c] [-n] [-l] <muster> <datei>\n");
        return 2;
    }

    const char *pattern = argv[arg_idx];
    const char *filepath = argv[arg_idx + 1];

    char buf[32768];
    int n = velo_read_file(filepath, buf, sizeof(buf) - 1);
    if (n < 0) {
        printf("\033[31mgrep: %s: Datei nicht gefunden\033[0m\n", filepath);
        return 2;
    }
    buf[n] = '\0';

    int match_count = 0;
    int line_number = 1;

    // Eigenständiges zeilenweises Parsen ohne strtok
    int line_start = 0;
    for (int i = 0; i <= n; i++) {
        if (buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\0') {
            if (i > line_start) {
                char line[512];
                int len = i - line_start;
                if (len > 511) len = 511;
                memcpy(line, &buf[line_start], len);
                line[len] = '\0';

                int has = opt_i ? str_contains_icase(line, pattern) : (strstr(line, pattern) != NULL);
                if (opt_v) has = !has;

                if (has) {
                    match_count++;
                    if (opt_l) {
                        printf("%s\n", filepath);
                        return 0;
                    }
                    if (!opt_c) {
                        if (opt_n) printf("\033[1;32m%d:\033[0m%s\n", line_number, line);
                        else printf("%s\n", line);
                    }
                }
                line_number++;
            }
            line_start = i + 1;
        }
    }

    if (opt_c) printf("%d\n", match_count);
    return (match_count > 0) ? 0 : 1;
}
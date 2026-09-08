#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <velo/syscall.h>

int main(int argc, char **argv) {
    int opt_n = 0, opt_b = 0, opt_s = 0, opt_E = 0;
    int file_idx = -1;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (int k = 1; argv[i][k]; k++) {
                if (argv[i][k] == 'n') opt_n = 1;
                else if (argv[i][k] == 'b') opt_b = 1;
                else if (argv[i][k] == 's') opt_s = 1;
                else if (argv[i][k] == 'E') opt_E = 1;
            }
        } else if (file_idx == -1) {
            file_idx = i;
        }
    }

    if (file_idx == -1) {
        printf("cat: Syntax: cat [-n] [-b] [-s] [-E] <datei>\n");
        return 1;
    }

    char buf[32768];
    int n = velo_read_file(argv[file_idx], buf, sizeof(buf) - 1);
    if (n < 0) {
        printf("\033[31mcat: %s: Datei oder Verzeichnis nicht gefunden\033[0m\n", argv[file_idx]);
        return 1;
    }
    buf[n] = '\0';

    int line_no = 1;
    int at_line_start = 1;
    int last_was_empty = 0;

    for (int i = 0; i < n; i++) {
        if (at_line_start) {
            int is_empty = (buf[i] == '\n' || (buf[i] == '\r' && buf[i+1] == '\n'));
            if (opt_s && is_empty && last_was_empty) {
                if (buf[i] == '\r') i++;
                continue;
            }
            last_was_empty = is_empty;

            if (opt_n || (opt_b && !is_empty)) {
                printf("%6d  ", line_no++);
            }
            at_line_start = 0;
        }

        if (buf[i] == '\r') continue;
        if (buf[i] == '\n') {
            if (opt_E) putchar('$');
            putchar('\n');
            at_line_start = 1;
        } else {
            putchar(buf[i]);
        }
    }
    if (n > 0 && buf[n-1] != '\n') putchar('\n');
    return 0;
}
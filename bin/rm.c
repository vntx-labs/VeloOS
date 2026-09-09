#include <stdio.h>
#include <string.h>
#include <velo/syscall.h>

int main(int argc, char **argv) {
    int opt_f = 0, opt_v = 0, opt_r = 0;
    int start = 1;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (int k = 1; argv[i][k]; k++) {
                if (argv[i][k] == 'f') opt_f = 1;
                else if (argv[i][k] == 'v') opt_v = 1;
                else if (argv[i][k] == 'r' || argv[i][k] == 'R') opt_r = 1;
            }
        } else {
            start = i;
            break;
        }
    }

    if (start >= argc) {
        if (!opt_f) printf("rm: Fehlender Operand\nSyntax: rm [-r] [-f] [-v] <datei...>\n");
        return opt_f ? 0 : 1;
    }

    (void)opt_r;
    int status = 0;
    for (int i = start; i < argc; i++) {
        if (!velo_delete_file(argv[i])) {
            if (!opt_f) {
                printf("\033[31mrm: '%s' kann nicht entfernt werden: Datei oder Verzeichnis nicht gefunden\033[0m\n", argv[i]);
                status = 1;
            }
        } else if (opt_v) {
            printf("'%s' wurde entfernt\n", argv[i]);
        }
    }
    return status;
}
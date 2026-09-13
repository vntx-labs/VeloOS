#include <stdio.h>
#include <string.h>
#include <velo/syscall.h>

int main(int argc, char **argv) {
    int opt_c = 0;
    int start = 1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--no-create")) opt_c = 1;
        else if (argv[i][0] == '-') { /* -a, -m */ }
        else { start = i; break; }
    }

    if (start >= argc) {
        printf("touch: Fehlender Datei-Operand\n");
        return 1;
    }

    for (int i = start; i < argc; i++) {
        if (!opt_c) {
            if (!velo_create_file(argv[i])) {
                printf("\033[31mtouch: '%s' kann nicht beruehrt/angelegt werden\033[0m\n", argv[i]);
            }
        }
    }
    return 0;
}
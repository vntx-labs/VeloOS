#include <stdio.h>
#include <string.h>
#include <velo/syscall.h>

static int make_parents(char *path) {
    for (char *p = path + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char temp = *p;
            *p = '\0';
            velo_mkdir(path);
            *p = temp;
        }
    }
    return velo_mkdir(path);
}

int main(int argc, char **argv) {
    int opt_p = 0, opt_v = 0;
    int start = 1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--parents")) opt_p = 1;
        else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose")) opt_v = 1;
        else { start = i; break; }
    }

    if (start >= argc) {
        printf("mkdir: Fehlender Operand\nSyntax: mkdir [-p] [-v] <verzeichnis...>\n");
        return 1;
    }

    int ret = 0;
    for (int i = start; i < argc; i++) {
        char path_copy[128];
        strncpy(path_copy, argv[i], sizeof(path_copy) - 1);
        path_copy[sizeof(path_copy) - 1] = '\0';

        int ok = opt_p ? make_parents(path_copy) : velo_mkdir(argv[i]);
        if (!ok) {
            printf("\033[31mmkdir: Verzeichnis '%s' konnte nicht erstellt werden\033[0m\n", argv[i]);
            ret = 1;
        } else if (opt_v) {
            printf("mkdir: Verzeichnis '%s' erstellt\n", argv[i]);
        }
    }
    return ret;
}
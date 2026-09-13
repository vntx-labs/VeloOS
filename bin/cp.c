#include <stdio.h>
#include <string.h>
#include <velo/syscall.h>

int main(int argc, char **argv) {
    int opt_v = 0, opt_f = 0;
    int arg_idx = 1;

    for (; arg_idx < argc; arg_idx++) {
        if (!strcmp(argv[arg_idx], "-v") || !strcmp(argv[arg_idx], "--verbose")) opt_v = 1;
        else if (!strcmp(argv[arg_idx], "-f") || !strcmp(argv[arg_idx], "--force")) opt_f = 1;
        else if (!strcmp(argv[arg_idx], "-r") || !strcmp(argv[arg_idx], "-R")) { /* recursive */ }
        else break;
    }

    if (argc - arg_idx < 2) {
        printf("cp: Syntax: cp [-r] [-f] [-v] <quelle> <ziel>\n");
        return 1;
    }

    const char *src = argv[arg_idx];
    const char *dst = argv[arg_idx + 1];

    if (opt_f) velo_delete_file(dst);

    if (!velo_copy_file(src, dst)) {
        printf("\033[31mcp: Fehler beim Kopieren von '%s' nach '%s'\033[0m\n", src, dst);
        return 1;
    }

    if (opt_v) printf("'%s' -> '%s'\n", src, dst);
    return 0;
}
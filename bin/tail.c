#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <velo/syscall.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("tail: Syntax: tail [-n zeilen] <datei>\n");
        return 1;
    }
    int max_lines = 10;
    const char *file = argv[1];
    if (argc >= 4 && !strcmp(argv[1], "-n")) {
        max_lines = atoi(argv[2]);
        file = argv[3];
    }

    char buf[16384];
    int n = velo_read_file(file, buf, sizeof(buf) - 1);
    if (n < 0) {
        printf("\033[31mtail: %s: Datei nicht gefunden\033[0m\n", file);
        return 1;
    }
    buf[n] = '\0';
    int total_lines = 0;
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n') total_lines++;
    }

    int skip = total_lines - max_lines;
    if (skip < 0) skip = 0;

    int passed = 0;
    for (int i = 0; i < n; i++) {
        if (passed >= skip) putchar(buf[i]);
        if (buf[i] == '\n') passed++;
    }
    if (n > 0 && buf[n-1] != '\n') putchar('\n');
    return 0;
}
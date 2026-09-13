#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <velo/syscall.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("head: Syntax: head [-n zeilen] <datei>\n");
        return 1;
    }
    int max_lines = 10;
    const char *file = argv[1];
    if (argc >= 4 && !strcmp(argv[1], "-n")) {
        max_lines = atoi(argv[2]);
        file = argv[3];
    } else if (argc >= 3 && argv[1][0] == '-') {
        max_lines = atoi(&argv[1][1]);
        file = argv[2];
    }

    char buf[16384];
    int n = velo_read_file(file, buf, sizeof(buf) - 1);
    if (n < 0) {
        printf("\033[31mhead: %s: Datei nicht gefunden\033[0m\n", file);
        return 1;
    }
    buf[n] = '\0';
    int count = 0;
    for (int i = 0; buf[i] && count < max_lines; i++) {
        putchar(buf[i]);
        if (buf[i] == '\n') count++;
    }
    if (n > 0 && buf[n-1] != '\n') putchar('\n');
    return 0;
}
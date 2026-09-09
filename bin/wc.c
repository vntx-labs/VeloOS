#include <stdio.h>
#include <ctype.h>
#include <velo/syscall.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("wc: Syntax: wc <datei>\n");
        return 1;
    }
    char buf[16384];
    int n = velo_read_file(argv[1], buf, sizeof(buf) - 1);
    if (n < 0) {
        printf("\033[31mwc: %s: Datei nicht gefunden\033[0m\n", argv[1]);
        return 1;
    }
    buf[n] = '\0';
    int lines = 0, words = 0, in_word = 0;
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n') lines++;
        if (isspace((unsigned char)buf[i])) {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
    }
    printf(" %d  %d %d %s\n", lines, words, n, argv[1]);
    return 0;
}
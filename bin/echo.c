#include <stdio.h>
#include <string.h>

static void print_escaped(const char *str) {
    while (*str) {
        if (*str == '\\' && *(str + 1)) {
            str++;
            if (*str == 'n') putchar('\n');
            else if (*str == 't') putchar('\t');
            else if (*str == 'r') putchar('\r');
            else if (*str == 'e' || *str == '0') printf("\033");
            else putchar(*str);
        } else {
            putchar(*str);
        }
        str++;
    }
}

int main(int argc, char **argv) {
    int opt_n = 0, opt_e = 0;
    int start = 1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n")) opt_n = 1;
        else if (!strcmp(argv[i], "-e")) opt_e = 1;
        else if (!strcmp(argv[i], "-E")) opt_e = 0;
        else { start = i; break; }
    }

    for (int i = start; i < argc; i++) {
        if (i > start) putchar(' ');
        if (opt_e) print_escaped(argv[i]);
        else printf("%s", argv[i]);
    }
    if (!opt_n) putchar('\n');
    return 0;
}
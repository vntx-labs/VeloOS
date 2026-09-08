#include <stdio.h>
#include <string.h>
#include <velo/syscall.h>

int main(int argc, char **argv) {
    int opt_a = 0, opt_s = 0, opt_n = 0, opt_r = 0, opt_m = 0;

    if (argc == 1) opt_s = 1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-a") || !strcmp(argv[i], "--all")) opt_a = 1;
        else if (!strcmp(argv[i], "-s")) opt_s = 1;
        else if (!strcmp(argv[i], "-n")) opt_n = 1;
        else if (!strcmp(argv[i], "-r")) opt_r = 1;
        else if (!strcmp(argv[i], "-m") || !strcmp(argv[i], "-p")) opt_m = 1;
    }

    VeloSysInfo info;
    char host[32] = "velo-pc";
    if (velo_get_sysinfo(&info) == 0 && info.pc_name[0]) {
        strncpy(host, info.pc_name, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
    }

    if (opt_a) {
        printf("VeloOS %s 11.0.0-velo SMP 2026 x86_64 GNU/Velo\n", host);
        return 0;
    }

    int printed = 0;
    if (opt_s) { printf("VeloOS"); printed = 1; }
    if (opt_n) { if (printed) printf(" "); printf("%s", host); printed = 1; }
    if (opt_r) { if (printed) printf(" "); printf("11.0.0-velo"); printed = 1; }
    if (opt_m) { if (printed) printf(" "); printf("x86_64"); printed = 1; }
    printf("\n");
    return 0;
}
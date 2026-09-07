#include <stdio.h>
#include <string.h>
#include <velo/syscall.h>

int main(int argc, char **argv) {
    int opt_h = 0, opt_g = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--human")) opt_h = 1;
        else if (!strcmp(argv[i], "-g")) opt_g = 1;
    }

    VeloSysInfo info;
    if (velo_get_sysinfo(&info) == 0) {
        unsigned int total_mb = (unsigned int)info.total_ram_mb;
        unsigned int used_mb = (total_mb > 128) ? 128 : (total_mb / 2);
        unsigned int free_mb = (total_mb > used_mb) ? (total_mb - used_mb) : 0;

        printf("\033[1;36m               Gesamt       Benutzt          Frei\033[0m\n");
        if (opt_h || opt_g) {
            printf("Speicher:       %4uGi         %4uMi        %4uGi\n", 
                   total_mb / 1024, used_mb, free_mb / 1024);
        } else {
            printf("Speicher:    %6u MB     %6u MB     %6u MB\n", total_mb, used_mb, free_mb);
        }
        printf("Swap:             0 MB          0 MB          0 MB\n");
    }
    return 0;
}
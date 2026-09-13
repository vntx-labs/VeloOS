#include <stdio.h>
#include <string.h>
#include <velo/syscall.h>

static void format_human(unsigned long long bytes, char *out) {
    if (bytes >= 1024ULL * 1024 * 1024) sprintf(out, "%4lluG", bytes / (1024ULL * 1024 * 1024));
    else if (bytes >= 1024ULL * 1024) sprintf(out, "%4lluM", bytes / (1024ULL * 1024));
    else if (bytes >= 1024ULL) sprintf(out, "%4lluK", bytes / 1024ULL);
    else sprintf(out, "%5llu", bytes);
}

int main(int argc, char **argv) {
    int opt_h = 0, opt_m = 0, opt_T = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--human-readable")) opt_h = 1;
        else if (!strcmp(argv[i], "-m")) opt_m = 1;
        else if (!strcmp(argv[i], "-T") || !strcmp(argv[i], "--print-type")) opt_T = 1;
    }

    if (opt_T) printf("\033[1;36mDateisystem     Typ   1K-Bloecke    Benutzt  Verfuegbar Ben%% Eingeh. auf\033[0m\n");
    else if (opt_h) printf("\033[1;36mDateisystem      Groesse  Benutzt   Verf.  Ben%% Eingeh. auf\033[0m\n");
    else printf("\033[1;36mDateisystem     1K-Bloecke    Benutzt  Verfuegbar Ben%% Eingeh. auf\033[0m\n");

    for (int i = 0; i < 8; i++) {
        VeloDriveInfo d;
        if (velo_get_drive_info(i, &d) == 0 && d.total_bytes > 0) {
            unsigned long long tot = d.total_bytes;
            unsigned long long free = d.free_bytes;
            unsigned long long used = (tot > free) ? (tot - free) : 0;
            int pct = (tot > 0) ? (int)((used * 100ULL) / tot) : 0;

            if (opt_h) {
                char s_tot[16], s_use[16], s_fre[16];
                format_human(tot, s_tot);
                format_human(used, s_use);
                format_human(free, s_fre);
                printf("/dev/sd%c1       %8s %8s %8s %4d%% %s/\n", 'a' + i, s_tot, s_use, s_fre, pct, d.label);
            } else if (opt_m) {
                printf("/dev/sd%c1     %11llu%11llu%12llu %3d%% %s/\n",
                       'a' + i, tot / 1048576ULL, used / 1048576ULL, free / 1048576ULL, pct, d.label);
            } else {
                if (opt_T) {
                    printf("/dev/sd%c1     vfat  %11llu%11llu%12llu %3d%% %s/\n",
                           'a' + i, tot / 1024ULL, used / 1024ULL, free / 1024ULL, pct, d.label);
                } else {
                    printf("/dev/sd%c1     %11llu%11llu%12llu %3d%% %s/\n",
                           'a' + i, tot / 1024ULL, used / 1024ULL, free / 1024ULL, pct, d.label);
                }
            }
        }
    }
    return 0;
}
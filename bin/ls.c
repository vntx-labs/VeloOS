#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <velo/syscall.h>

static void format_h(unsigned int bytes, char *out) {
    if (bytes >= 1024 * 1024) sprintf(out, "%4uM", bytes / (1024 * 1024));
    else if (bytes >= 1024) sprintf(out, "%4uK", bytes / 1024);
    else sprintf(out, "%5u", bytes);
}

int main(int argc, char **argv) {
    int opt_l = 0, opt_a = 0, opt_h = 0, opt_1 = 0, opt_r = 0;
    const char *target_dir = ".";

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (int k = 1; argv[i][k]; k++) {
                if (argv[i][k] == 'l') opt_l = 1;
                else if (argv[i][k] == 'a') opt_a = 1;
                else if (argv[i][k] == 'h') opt_h = 1;
                else if (argv[i][k] == '1') opt_1 = 1;
                else if (argv[i][k] == 'r') opt_r = 1;
            }
        } else {
            target_dir = argv[i];
        }
    }

    VeloDirEntry entries[256];
    int count = velo_list_dir(target_dir, entries, 256);
    if (count < 0) {
        printf("\033[31mls: Zugriff auf '%s' nicht moeglich: Datei oder Verzeichnis nicht gefunden\033[0m\n", target_dir);
        return 1;
    }

    int start = opt_r ? (count - 1) : 0;
    int end   = opt_r ? -1 : count;
    int step  = opt_r ? -1 : 1;

    for (int i = start; i != end; i += step) {
        if (!opt_a && entries[i].name[0] == '.') continue;

        if (opt_l) {
            char sz_str[16];
            if (opt_h) format_h(entries[i].size, sz_str);
            else sprintf(sz_str, "%8u", (unsigned int)entries[i].size);

            const char *type = entries[i].is_dir ? "drwxr-xr-x" : "-rw-r--r--";
            const char *col = entries[i].is_dir ? "\033[1;34m" : "\033[0m";

            int day = entries[i].date & 0x1F;
            int mon = (entries[i].date >> 5) & 0x0F;
            int yr  = ((entries[i].date >> 9) & 0x7F) + 1980;

            printf("%s 1 user user %s %02d.%02d.%04d %s%s\033[0m\n", 
                   type, sz_str, day, mon, yr, col, entries[i].name);
        } else if (opt_1) {
            printf("%s%s\033[0m\n", entries[i].is_dir ? "\033[1;34m" : "\033[0m", entries[i].name);
        } else {
            printf("%s%-18s\033[0m", entries[i].is_dir ? "\033[1;34m" : "\033[0m", entries[i].name);
        }
    }
    if (!opt_l && !opt_1 && count > 0) printf("\n");

    return 0;
}
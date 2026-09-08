#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <velo/syscall.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("kill: Syntax: kill [-s signal | -signal] <pid...>\n");
        return 1;
    }

    if (!strcmp(argv[1], "-l")) {
        printf(" 1) SIGHUP   2) SIGINT   3) SIGQUIT  9) SIGKILL  15) SIGTERM\n");
        return 0;
    }

    int start = 1;
    if (argv[1][0] == '-') start = 2;

    int ret = 0;
    for (int i = start; i < argc; i++) {
        int pid = atoi(argv[i]);
        if (pid <= 0) {
            printf("\033[31mkill: PID 0 (Kernel) kann nicht beendet werden\033[0m\n");
            ret = 1;
            continue;
        }

        if (velo_kill_task(pid) != 0) {
            printf("\033[31mkill: Prozess (%d) nicht gefunden\033[0m\n", pid);
            ret = 1;
        } else {
            printf("Prozess %d beendet.\n", pid);
        }
    }
    return ret;
}
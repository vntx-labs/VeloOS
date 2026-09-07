#include <stdio.h>
#include <string.h>
#include <velo/syscall.h>

int main(int argc, char **argv) {
    int do_reboot = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "--reboot")) {
            do_reboot = 1;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "-P") || !strcmp(argv[i], "--poweroff")) {
            do_reboot = 0;
        }
    }

    if (do_reboot) {
        printf("\033[1;33mSystem wird jetzt neu gestartet...\033[0m\n");
        velo_syscall(SYS_TASK_SLEEP, 5, 0, 0, 0);
        velo_reboot();
    } else {
        printf("\033[1;31mSystem wird jetzt heruntergefahren...\033[0m\n");
        velo_syscall(SYS_TASK_SLEEP, 5, 0, 0, 0);
        velo_shutdown();
    }

    return 0;
}
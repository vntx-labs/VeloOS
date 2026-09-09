#include <stdio.h>
#include <string.h>
#include <velo/syscall.h>

int main(int argc, char **argv) {
    int show_all = 0;
    int full_format = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "aux") || !strcmp(argv[i], "-ef") || !strcmp(argv[i], "-a") || !strcmp(argv[i], "-e")) {
            show_all = 1;
            full_format = 1;
        } else if (!strcmp(argv[i], "-f") || !strcmp(argv[i], "-u")) {
            full_format = 1;
        }
    }

    VeloTaskInfo tasks[16];
    int count = velo_get_tasks(tasks, 16);
    if (count <= 0) {
        printf("\033[31mps: Fehler beim Abrufen der Prozesstabelle\033[0m\n");
        return 1;
    }

    if (full_format) {
        printf("\033[1;36mUSER       PID  STAT    ZEIT BEFEHL\033[0m\n");
        for (int i = 0; i < count; i++) {
            if (!show_all && tasks[i].pid == 0) continue;

            const char *user = tasks[i].is_user ? "user    " : "root    ";
            const char *stat = (tasks[i].state == 2) ? "R   " : ((tasks[i].state == 3) ? "S   " : "I   ");
            unsigned int sec = tasks[i].cpu_ticks / 100;
            unsigned int min = sec / 60;
            sec %= 60;

            printf("%s %4d  %s %02u:%02u %s\n", user, tasks[i].pid, stat, min, sec, tasks[i].name);
        }
    } else {
        printf("\033[1;36m  PID TTY          ZEIT BEFEHL\033[0m\n");
        for (int i = 0; i < count; i++) {
            if (!show_all && tasks[i].pid == 0) continue;

            unsigned int sec = tasks[i].cpu_ticks / 100;
            unsigned int min = sec / 60;
            sec %= 60;

            printf("%5d %-8s %02u:%02u %s\n", tasks[i].pid, tasks[i].is_user ? "tty1" : "?", min, sec, tasks[i].name);
        }
    }
    return 0;
}
#include <stdio.h>
#include <string.h>
#include <velo/syscall.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("\033[1;33mReboot wird ausgefuehrt...\033[0m\n");
    velo_syscall(SYS_TASK_SLEEP, 5, 0, 0, 0);
    velo_reboot();
    return 0;
}
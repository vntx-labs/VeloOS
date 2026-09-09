#include <stdio.h>
#include <velo/syscall.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    VeloSysInfo info;
    if (velo_get_sysinfo(&info) == 0 && info.user_name[0]) {
        printf("%s\n", info.user_name);
    } else {
        printf("user\n");
    }
    return 0;
}
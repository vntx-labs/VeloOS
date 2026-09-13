#include <stdio.h>
#include <velo/syscall.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    VeloSysInfo info;
    if (velo_get_sysinfo(&info) == 0 && info.pc_name[0]) {
        printf("%s\n", info.pc_name);
    } else {
        printf("velo-pc\n");
    }
    return 0;
}
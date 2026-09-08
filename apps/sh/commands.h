#ifndef VELO_SH_COMMANDS_H
#define VELO_SH_COMMANDS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <velo/syscall.h>

typedef struct {
    char *cwd;
    void (*print)(const char *text);
    void (*print_raw)(const char *text);
    void (*build_path)(const char *in, char *out, size_t max);
    const char *(*env_get)(const char *key);
    void (*env_set)(const char *key, const char *val);
} ShellContext;

typedef int (*CmdFunc)(int argc, char **argv, ShellContext *ctx);

typedef struct {
    const char *name;
    const char *alias;
    CmdFunc     func;
    const char *desc;
} CommandEntry;

/* Deklarationen aller Befehle */
int cmd_help(int argc, char **argv, ShellContext *ctx);
int cmd_cd(int argc, char **argv, ShellContext *ctx);
int cmd_echo(int argc, char **argv, ShellContext *ctx);
int cmd_ls(int argc, char **argv, ShellContext *ctx);
int cmd_cat(int argc, char **argv, ShellContext *ctx);
int cmd_touch(int argc, char **argv, ShellContext *ctx);
int cmd_mkdir(int argc, char **argv, ShellContext *ctx);
int cmd_rm(int argc, char **argv, ShellContext *ctx);
int cmd_cp(int argc, char **argv, ShellContext *ctx);
int cmd_mv(int argc, char **argv, ShellContext *ctx);
int cmd_sysinfo(int argc, char **argv, ShellContext *ctx);
int cmd_whoami(int argc, char **argv, ShellContext *ctx);
int cmd_hostname(int argc, char **argv, ShellContext *ctx);
int cmd_uname(int argc, char **argv, ShellContext *ctx);
int cmd_codes(int argc, char **argv, ShellContext *ctx);
int cmd_ifconfig(int argc, char **argv, ShellContext *ctx);

/* Neue Befehle */
int cmd_grep(int argc, char **argv, ShellContext *ctx);
int cmd_head(int argc, char **argv, ShellContext *ctx);
int cmd_tail(int argc, char **argv, ShellContext *ctx);
int cmd_wc(int argc, char **argv, ShellContext *ctx);
int cmd_sort(int argc, char **argv, ShellContext *ctx);
int cmd_uniq(int argc, char **argv, ShellContext *ctx);
int cmd_find(int argc, char **argv, ShellContext *ctx);
int cmd_ln(int argc, char **argv, ShellContext *ctx);
int cmd_chmod(int argc, char **argv, ShellContext *ctx);
int cmd_chown(int argc, char **argv, ShellContext *ctx);
int cmd_ps(int argc, char **argv, ShellContext *ctx);
int cmd_kill(int argc, char **argv, ShellContext *ctx);
int cmd_top(int argc, char **argv, ShellContext *ctx);
int cmd_df(int argc, char **argv, ShellContext *ctx);
int cmd_du(int argc, char **argv, ShellContext *ctx);
int cmd_free(int argc, char **argv, ShellContext *ctx);
int cmd_date(int argc, char **argv, ShellContext *ctx);
int cmd_ping(int argc, char **argv, ShellContext *ctx);
int cmd_curl(int argc, char **argv, ShellContext *ctx);
int cmd_wget(int argc, char **argv, ShellContext *ctx);
int cmd_netstat(int argc, char **argv, ShellContext *ctx);

#endif
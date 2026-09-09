#ifndef KSHELL_H
#define KSHELL_H

#include <efi.h>
#include <efilib.h>

void kshell_init(void);
void kshell_start(int tty_num);
void kshell_emergency_crash(int tty_num);
void kshell_tick_frame(int tty_num);
void kshell_handle_key(int tty_num, char key);
void kshell_mark_dirty(void);
int  kshell_is_active(void);

/* Kernel Session Lifecycle API */
void kernel_session_start(int session_id, const char *username);
void kernel_session_end(int session_id);
int  kernel_session_is_active(int session_id);

#endif
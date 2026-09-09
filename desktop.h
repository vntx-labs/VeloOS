#ifndef DESKTOP_H
#define DESKTOP_H

#include <efi.h>
#include <efilib.h>

#ifdef __cplusplus
extern "C" {
#endif

void desktop_start(void);
void desktop_tick_frame(void);
void desktop_handle_key(char c);

#ifdef __cplusplus
}
#endif

#endif
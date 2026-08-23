#ifndef DESKTOP_H
#define DESKTOP_H

#include <efi.h>
#include <efilib.h>

void desktop_start(void);
void desktop_handle_key(char c);

#endif
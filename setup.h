#ifndef SETUP_H
#define SETUP_H

#include <efi.h>
#include <efilib.h>

typedef struct __attribute__((packed)) {
    char magic[8]; // "VELO_CFG"
    char username[32];
    char password[32];
    char pcname[32];
    UINT32 timezone_idx;
    UINT32 dst_auto;
    UINT32 lang;
    UINT32 avatar;
    UINT32 setup_completed;
} SystemConfig;

void setup_init(void);
void setup_disable(void);
void setup_tick(void);
void setup_handle_key(char c);
int  setup_is_active(void);
int  load_system_config(SystemConfig *out_cfg);

#endif
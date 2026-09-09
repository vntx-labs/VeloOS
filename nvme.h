#ifndef NVME_H
#define NVME_H

#include <efi.h>
#include <efilib.h>

#define MAX_NVME_DEVICES 8

typedef struct {
    UINTN  bar0;
    UINT32 nsid;
    UINT64 sector_count;
    UINT32 sector_size;
    char   model[41];
    int    active;
} NVME_DEVICE_INFO;

int  init_nvme(void);
int  nvme_get_device_count(void);
NVME_DEVICE_INFO* nvme_get_device_info(int index);
void* nvme_get_device(int index);

int nvme_read_sector(void *dev, UINT32 start_lba_low, UINT32 start_lba_high, UINT32 sector_count, void *buf);
int nvme_write_sector(void *dev, UINT32 start_lba_low, UINT32 start_lba_high, UINT32 sector_count, void *buf);

#endif
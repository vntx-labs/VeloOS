#ifndef AHCI_H
#define AHCI_H

#include <efi.h>
#include <efilib.h>

#define MAX_AHCI_PORTS 32

typedef struct {
    void *port_addr;
    int port_number;
    int active;
    UINT64 sector_count;
    char model[41];
    int is_nvme;
} AHCI_PORT_INFO;

void init_ahci(UINTN abar_address);
int  ahci_get_port_count(void);
AHCI_PORT_INFO* ahci_get_port_info(int index);
void* ahci_get_port(int index);
int  ahci_identify_drive(void *port, int port_no, AHCI_PORT_INFO *info);

int read_sata_sector(void *port, UINT32 start_lba_low, UINT32 start_lba_high, 
                     UINT32 sector_count, void *buf);
int write_sata_sector(void *port, UINT32 start_lba_low, UINT32 start_lba_high, 
                      UINT32 sector_count, void *buf);

#endif
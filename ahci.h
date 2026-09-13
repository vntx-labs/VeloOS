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
    int is_hidden; // 1 = Installationsmedium (ausgeblendet)
} AHCI_PORT_INFO;

void init_ahci(UINTN abar_address);
int  ahci_get_port_count(void);
AHCI_PORT_INFO* ahci_get_port_info(int index);
void* ahci_get_port(int index);

void* ahci_get_primary_port(void);
void  ahci_set_primary_port(void *port);
void* ahci_get_boot_port(void);
void  ahci_set_port_hidden(void *port, int hidden);

int  ahci_identify_drive(void *port, int port_no, AHCI_PORT_INFO *info);

int read_sata_sector(void *port, UINT32 start_lba_low, UINT32 start_lba_high, 
                     UINT32 sector_count, void *buf);
int write_sata_sector(void *port, UINT32 start_lba_low, UINT32 start_lba_high, 
                      UINT32 sector_count, void *buf);

#endif
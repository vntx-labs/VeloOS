#ifndef AHCI_H
#define AHCI_H

#include <efi.h>
#include <efilib.h>

// Funktion zur Initialisierung des AHCI-Controllers über die BAR5-Speicheradresse (ABAR)
void init_ahci(UINTN abar_address);

#endif
#include "ahci.h"

/*
 * VeloOS AHCI driver
 *
 * Wichtige Eigenschaften dieser Version:
 * - keine festen DMA-Adressen wie 0x100000 mehr
 * - 64-Bit-Adressen werden bei CLB/FB/CTBA/PRDT korrekt aufgeteilt
 * - Command List / FIS / Command Tables liegen in statisch reserviertem,
 *   ausgerichtetem Speicher und bleiben auch nach ExitBootServices() gültig
 * - der DMA-Engine wird korrekt mit FRE + ST gestartet
 * - nur ein PRDT-Eintrag wird verwendet; maximal 4 MiB pro Request
 * - alle Requests haben ein Timeout, damit ein defektes Gerät das OS nicht
 *   in einer Endlosschleife einfriert
 */

typedef volatile struct tagHBA_PORT {
    UINT32 clb;
    UINT32 clbu;
    UINT32 fb;
    UINT32 fbu;
    UINT32 is;
    UINT32 ie;
    UINT32 cmd;
    UINT32 rsv0;
    UINT32 tfd;
    UINT32 sig;
    UINT32 ssts;
    UINT32 sctl;
    UINT32 serr;
    UINT32 sact;
    UINT32 ci;
    UINT32 sntf;
    UINT32 fbs;
    UINT32 rsv1[11];
    UINT32 vendor[4];
} HBA_PORT;

typedef volatile struct tagHBA_MEM {
    UINT32 cap;
    UINT32 ghc;
    UINT32 is;
    UINT32 pi;
    UINT32 vs;
    UINT32 ccc_ctl;
    UINT32 ccc_ports;
    UINT32 em_loc;
    UINT32 em_ctl;
    UINT32 cap2;
    UINT32 bohc;
    UINT8  rsv[116];
    UINT8  vendor[96];
    HBA_PORT ports[32];
} HBA_MEM;

typedef struct tagHBA_CMD_HEADER {
    UINT8  cfl:5;
    UINT8  a:1;
    UINT8  w:1;
    UINT8  p:1;
    UINT8  r:1;
    UINT8  b:1;
    UINT8  c:1;
    UINT8  rsv0:1;
    UINT8  pmp:4;
    UINT16 prdtl;
    UINT32 prdbc;
    UINT32 ctba;
    UINT32 ctbau;
    UINT32 rsv1[4];
} HBA_CMD_HEADER;

typedef struct tagHBA_PRDT_ENTRY {
    UINT32 dba;
    UINT32 dbau;
    UINT32 rsv0;
    UINT32 dbc:22;
    UINT32 rsv1:9;
    UINT32 i:1;
} HBA_PRDT_ENTRY;

typedef struct tagHBA_CMD_TABLE {
    UINT8  cfis[64];
    UINT8  acmd[16];
    UINT8  rsv[48];
    HBA_PRDT_ENTRY prdt_entry[1];
} HBA_CMD_TABLE;

#define FIS_TYPE_REG_H2D       0x27
#define ATA_CMD_READ_DMA_EXT   0x25
#define ATA_CMD_WRITE_DMA_EXT  0x35

#define PORT_CMD_ST             (1U << 0)
#define PORT_CMD_FRE            (1U << 4)
#define PORT_CMD_FR             (1U << 14)
#define PORT_CMD_CR             (1U << 15)
#define PORT_IS_TFES            (1U << 30)

#define AHCI_TIMEOUT            100000000U
#define AHCI_MAX_SECTORS        8192U       /* 4 MiB, max. PRDT byte count */

/*
 * Statischer DMA-Speicher. Die Adresse des Kernel-Images ist auf der
 * UEFI-Maschine normal unterhalb von 4 GiB. Die Hardware bekommt trotzdem
 * immer beide 32-Bit-Hälften der Adresse.
 */
static HBA_CMD_HEADER g_cmd_lists[32][32] __attribute__((aligned(1024)));
static UINT8 g_fis_area[32][256] __attribute__((aligned(256)));
static HBA_CMD_TABLE g_cmd_tables[32][32] __attribute__((aligned(128)));

static inline UINT64 make_u64(UINT32 low, UINT32 high) {
    return ((UINT64)high << 32) | (UINT64)low;
}

static inline void split_u64(UINT64 value, UINT32 *low, UINT32 *high) {
    *low = (UINT32)(value & 0xFFFFFFFFULL);
    *high = (UINT32)(value >> 32);
}

static inline void outl(unsigned short port, unsigned int val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned int inl(unsigned short port) {
    unsigned int ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static UINT32 pci_config_read(UINT8 bus, UINT8 slot, UINT8 func, UINT8 offset) {
    UINT32 address = (1U << 31) |
                     ((UINT32)bus << 16) |
                     ((UINT32)(slot & 0x1F) << 11) |
                     ((UINT32)(func & 0x07) << 8) |
                     (UINT32)(offset & 0xFC);
    outl(0xCF8, address);
    return inl(0xCFC);
}

static HBA_MEM *find_ahci_controller(void) {
    for (UINT16 bus = 0; bus < 256; bus++) {
        for (UINT8 slot = 0; slot < 32; slot++) {
            for (UINT8 func = 0; func < 8; func++) {
                UINT32 vendor_device = pci_config_read(bus, slot, func, 0x00);
                if ((vendor_device & 0xFFFFU) == 0xFFFFU) {
                    if (func == 0) break;
                    continue;
                }

                UINT32 class_code = pci_config_read(bus, slot, func, 0x08);
                UINT8 base_class = (UINT8)((class_code >> 24) & 0xFF);
                UINT8 sub_class  = (UINT8)((class_code >> 16) & 0xFF);

                if (base_class != 0x01 || sub_class != 0x06) continue;

                UINT32 bar5_low = pci_config_read(bus, slot, func, 0x24);
                UINT64 abar = (UINT64)(bar5_low & 0xFFFFFFF0U);

                /* BAR5 ist bei AHCI normalerweise MMIO. Bei 64-Bit BARs
                   kommt die obere Hälfte aus 0x28. */
                if ((bar5_low & 0x6U) == 0x4U) {
                    UINT32 bar5_high = pci_config_read(bus, slot, func, 0x28);
                    abar |= ((UINT64)bar5_high << 32);
                }

                if (abar == 0) continue;
                return (HBA_MEM *)(UINTN)abar;
            }
        }
    }

    return (HBA_MEM *)0;
}

static int wait_port_clear(HBA_PORT *port, UINT32 mask) {
    for (UINT32 i = 0; i < AHCI_TIMEOUT; i++) {
        if ((port->cmd & mask) == 0) return 1;
    }
    return 0;
}

static int stop_cmd(HBA_PORT *port) {
    /* ST und FRE aus. */
    port->cmd &= ~(PORT_CMD_ST | PORT_CMD_FRE);

    if (!wait_port_clear(port, PORT_CMD_CR | PORT_CMD_FR)) {
        return 0;
    }

    return 1;
}

static int start_cmd(HBA_PORT *port) {
    /* FIS Receive muss vor Start gesetzt werden. */
    for (UINT32 i = 0; i < AHCI_TIMEOUT; i++) {
        if (!(port->cmd & PORT_CMD_CR)) break;
    }

    if (port->cmd & PORT_CMD_CR) return 0;

    port->cmd |= PORT_CMD_FRE;
    port->cmd |= PORT_CMD_ST;
    return 1;
}

static int init_port_memory(HBA_PORT *port, int port_no) {
    if (port_no < 0 || port_no >= 32) return 0;

    if (!stop_cmd(port)) return 0;

    __builtin_memset(g_cmd_lists[port_no], 0, sizeof(g_cmd_lists[port_no]));
    __builtin_memset(g_fis_area[port_no], 0, sizeof(g_fis_area[port_no]));
    __builtin_memset(g_cmd_tables[port_no], 0, sizeof(g_cmd_tables[port_no]));

    UINT32 clb_low, clb_high;
    UINT32 fb_low, fb_high;
    split_u64((UINT64)(UINTN)&g_cmd_lists[port_no][0], &clb_low, &clb_high);
    split_u64((UINT64)(UINTN)&g_fis_area[port_no][0], &fb_low, &fb_high);

    port->clb = clb_low;
    port->clbu = clb_high;
    port->fb = fb_low;
    port->fbu = fb_high;

    for (int i = 0; i < 32; i++) {
        HBA_CMD_HEADER *header = &g_cmd_lists[port_no][i];
        UINT32 ctba_low, ctba_high;

        split_u64((UINT64)(UINTN)&g_cmd_tables[port_no][i],
                  &ctba_low, &ctba_high);

        header->cfl = 5;      /* 20-byte Register H2D FIS */
        header->w = 0;
        header->prdtl = 1;    /* genau ein PRDT-Eintrag */
        header->ctba = ctba_low;
        header->ctbau = ctba_high;
    }

    /* Eventuelle alte Fehler quittieren. */
    port->is = 0xFFFFFFFFU;
    port->serr = 0xFFFFFFFFU;

    return start_cmd(port);
}

static int find_cmd_slot(HBA_PORT *port) {
    UINT32 slots = port->sact | port->ci;

    for (int i = 0; i < 32; i++) {
        if (!(slots & (1U << i))) return i;
    }

    return -1;
}

typedef struct tagFIS_REG_H2D {
    UINT8  fis_type;
    UINT8  pmport:4;
    UINT8  rsv0:3;
    UINT8  c:1;
    UINT8  command;
    UINT8  featurel;
    UINT8  lba0;
    UINT8  lba1;
    UINT8  lba2;
    UINT8  device;
    UINT8  lba3;
    UINT8  lba4;
    UINT8  lba5;
    UINT8  featureh;
    UINT8  countl;
    UINT8  counth;
    UINT8  icc;
    UINT8  control;
    UINT8  rsv1[4];
} FIS_REG_H2D;

static int sata_transfer(HBA_PORT *port,
                         UINT32 start_lba_low,
                         UINT32 start_lba_high,
                         UINT32 sector_count,
                         void *buf,
                         int write) {
    if (!port || !buf || sector_count == 0 || sector_count > AHCI_MAX_SECTORS) {
        return 0;
    }

    if (port->cmd & PORT_CMD_CR) {
        /* Normalzustand: Command engine läuft. */
    } else {
        if (!start_cmd(port)) return 0;
    }

    int slot = find_cmd_slot(port);
    if (slot < 0) return 0;

    UINT64 clb_addr = make_u64(port->clb, port->clbu);
    HBA_CMD_HEADER *cmd_header =
        (HBA_CMD_HEADER *)(UINTN)clb_addr + slot;

    UINT64 ctba_addr = make_u64(cmd_header->ctba, cmd_header->ctbau);
    HBA_CMD_TABLE *cmd_table = (HBA_CMD_TABLE *)(UINTN)ctba_addr;

    __builtin_memset(cmd_header, 0, sizeof(HBA_CMD_HEADER));
    __builtin_memset(cmd_table, 0, sizeof(HBA_CMD_TABLE));

    cmd_header->cfl = 5;
    cmd_header->w = write ? 1 : 0;
    cmd_header->prdtl = 1;

    UINT64 buffer_addr = (UINT64)(UINTN)buf;
    UINT32 buf_low, buf_high;
    split_u64(buffer_addr, &buf_low, &buf_high);

    cmd_table->prdt_entry[0].dba = buf_low;
    cmd_table->prdt_entry[0].dbau = buf_high;
    cmd_table->prdt_entry[0].dbc = (sector_count * 512U) - 1U;
    cmd_table->prdt_entry[0].i = 1;

    FIS_REG_H2D *fis = (FIS_REG_H2D *)(UINTN)&cmd_table->cfis[0];
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport = 0;
    fis->c = 1;
    fis->command = write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT;
    fis->featurel = 0;
    fis->featureh = 0;

    fis->lba0 = (UINT8)(start_lba_low & 0xFF);
    fis->lba1 = (UINT8)((start_lba_low >> 8) & 0xFF);
    fis->lba2 = (UINT8)((start_lba_low >> 16) & 0xFF);
    fis->device = 0x40; /* LBA mode */
    fis->lba3 = (UINT8)((start_lba_low >> 24) & 0xFF);
    fis->lba4 = (UINT8)(start_lba_high & 0xFF);
    fis->lba5 = (UINT8)((start_lba_high >> 8) & 0xFF);
    fis->countl = (UINT8)(sector_count & 0xFF);
    fis->counth = (UINT8)((sector_count >> 8) & 0xFF);

    /* Alte Portfehler quittieren, bevor der Request gestartet wird. */
    port->is = 0xFFFFFFFFU;

    UINT32 slot_mask = 1U << slot;
    port->ci |= slot_mask;

    for (UINT32 i = 0; i < AHCI_TIMEOUT; i++) {
        UINT32 is = port->is;

        if (is & PORT_IS_TFES) {
            port->ci &= ~slot_mask;
            return 0;
        }

        if (!(port->ci & slot_mask)) {
            if (port->is & PORT_IS_TFES) return 0;
            return 1;
        }
    }

    /* Timeout: Request ist nicht fertig geworden. */
    port->ci &= ~slot_mask;
    return 0;
}

int read_sata_sector(void *port,
                     UINT32 start_lba_low,
                     UINT32 start_lba_high,
                     UINT32 sector_count,
                     void *buf) {
    return sata_transfer((HBA_PORT *)port,
                         start_lba_low,
                         start_lba_high,
                         sector_count,
                         buf,
                         0);
}

int write_sata_sector(void *port,
                      UINT32 start_lba_low,
                      UINT32 start_lba_high,
                      UINT32 sector_count,
                      void *buf) {
    return sata_transfer((HBA_PORT *)port,
                         start_lba_low,
                         start_lba_high,
                         sector_count,
                         buf,
                         1);
}

void init_ahci(UINTN abar_address) {
    HBA_MEM *hba = (HBA_MEM *)(UINTN)abar_address;

    if (!hba) {
        hba = find_ahci_controller();
    }

    if (!hba) return;

    /* AHCI Enable */
    hba->ghc |= (1U << 31);

    UINT32 ports_implemented = hba->pi;

    for (int i = 0; i < 32; i++) {
        if (!(ports_implemented & (1U << i))) continue;

        HBA_PORT *port = &hba->ports[i];
        UINT32 ssts = port->ssts;
        UINT8 det = (UINT8)(ssts & 0x07);
        UINT8 ipm = (UINT8)((ssts >> 8) & 0x0F);

        /* SATA device present + active power state. */
        if (det != 3 || ipm != 1) continue;

        /* 0x00000101 = SATA, 0xEB140101 = ATAPI/SATAPI etc. */
        UINT32 sig = port->sig;
        if (sig != 0x00000101U) continue;

        init_port_memory(port, i);
    }
}

#include "ahci.h"
#include "nvme.h"

typedef volatile struct __attribute__((packed)) tagHBA_PORT {
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

typedef volatile struct __attribute__((packed)) tagHBA_MEM {
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

typedef struct __attribute__((packed)) tagHBA_CMD_HEADER {
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

typedef struct __attribute__((packed)) tagHBA_PRDT_ENTRY {
    UINT32 dba;
    UINT32 dbau;
    UINT32 rsv0;
    UINT32 dbc:22;
    UINT32 rsv1:9;
    UINT32 i:1;
} HBA_PRDT_ENTRY;

typedef struct __attribute__((packed, aligned(128))) tagHBA_CMD_TABLE {
    UINT8  cfis[64];
    UINT8  acmd[16];
    UINT8  rsv[48];
    HBA_PRDT_ENTRY prdt_entry[1];
    UINT8  padding[112];
} HBA_CMD_TABLE;

#define FIS_TYPE_REG_H2D       0x27
#define ATA_CMD_READ_DMA_EXT   0x25
#define ATA_CMD_WRITE_DMA_EXT  0x35
#define ATA_CMD_IDENTIFY       0xEC

#define PORT_CMD_ST             (1U << 0)
#define PORT_CMD_SUD            (1U << 1)
#define PORT_CMD_POD            (1U << 2)
#define PORT_CMD_FRE            (1U << 4)
#define PORT_CMD_FR             (1U << 14)
#define PORT_CMD_CR             (1U << 15)
#define PORT_IS_TFES            (1U << 30)

#define AHCI_TIMEOUT            50000U
#define AHCI_MAX_SECTORS        8192U

static HBA_CMD_HEADER g_cmd_lists[32][32] __attribute__((aligned(1024)));
static UINT8 g_fis_area[32][256] __attribute__((aligned(256)));
static HBA_CMD_TABLE g_cmd_tables[32][32] __attribute__((aligned(128)));
static UINT16 g_identify_buffer[32][256] __attribute__((aligned(512)));

static AHCI_PORT_INFO g_ports[MAX_AHCI_PORTS];
static int g_port_count = 0;

static inline void split_u64(UINT64 value, UINT32 *low, UINT32 *high) {
    *low = (UINT32)(value & 0xFFFFFFFFULL);
    *high = (UINT32)(value >> 32);
}

static inline void outl(unsigned short port, unsigned int val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned int inl(unsigned short port) {
    unsigned int ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

static UINT32 pci_config_read(UINT8 bus, UINT8 slot, UINT8 func, UINT8 offset) {
    UINT32 address = (1U << 31) | ((UINT32)bus << 16) | ((UINT32)(slot & 0x1F) << 11) | ((UINT32)(func & 0x07) << 8) | (UINT32)(offset & 0xFC);
    outl(0xCF8, address);
    return inl(0xCFC);
}

static void pci_config_write(UINT8 bus, UINT8 slot, UINT8 func, UINT8 offset, UINT32 val) {
    UINT32 address = (1U << 31) | ((UINT32)bus << 16) | ((UINT32)(slot & 0x1F) << 11) | ((UINT32)(func & 0x07) << 8) | (UINT32)(offset & 0xFC);
    outl(0xCF8, address);
    outl(0xCFC, val);
}

static void stop_cmd(HBA_PORT *port) {
    port->cmd &= ~PORT_CMD_ST;
    port->cmd &= ~PORT_CMD_FRE;
    for (UINT32 i = 0; i < 1000; i++) {
        if (!(port->cmd & (PORT_CMD_CR | PORT_CMD_FR))) break;
        io_wait();
    }
}

static void start_cmd(HBA_PORT *port) {
    for (UINT32 i = 0; i < 1000; i++) {
        if (!(port->cmd & PORT_CMD_CR)) break;
        io_wait();
    }
    port->cmd |= PORT_CMD_FRE;
    port->cmd |= PORT_CMD_ST;
}

static void init_port_memory(HBA_PORT *port, int mem_slot) {
    if (mem_slot < 0 || mem_slot >= 32) return;
    stop_cmd(port);

    __builtin_memset(g_cmd_lists[mem_slot], 0, sizeof(g_cmd_lists[mem_slot]));
    __builtin_memset(g_fis_area[mem_slot], 0, sizeof(g_fis_area[mem_slot]));
    __builtin_memset(g_cmd_tables[mem_slot], 0, sizeof(g_cmd_tables[mem_slot]));

    UINT32 clb_low, clb_high;
    UINT32 fb_low, fb_high;
    split_u64((UINT64)(UINTN)&g_cmd_lists[mem_slot][0], &clb_low, &clb_high);
    split_u64((UINT64)(UINTN)&g_fis_area[mem_slot][0], &fb_low, &fb_high);

    port->clb = clb_low;
    port->clbu = clb_high;
    port->fb = fb_low;
    port->fbu = fb_high;

    for (int i = 0; i < 32; i++) {
        HBA_CMD_HEADER *header = &g_cmd_lists[mem_slot][i];
        UINT32 ctba_low, ctba_high;
        split_u64((UINT64)(UINTN)&g_cmd_tables[mem_slot][i], &ctba_low, &ctba_high);

        header->cfl = 5;
        header->w = 0;
        header->prdtl = 1;
        header->ctba = ctba_low;
        header->ctbau = ctba_high;
    }

    port->is = 0xFFFFFFFFU;
    port->serr = 0xFFFFFFFFU;
    start_cmd(port);
}

static int get_mem_slot_from_port(HBA_PORT *port) {
    for (int i = 0; i < 32; i++) {
        UINT32 clb_low, clb_high;
        split_u64((UINT64)(UINTN)&g_cmd_lists[i][0], &clb_low, &clb_high);
        if (port->clb == clb_low && port->clbu == clb_high) {
            return i;
        }
    }
    return -1;
}

static int find_cmd_slot(HBA_PORT *port) {
    UINT32 slots = port->sact | port->ci;
    for (int i = 0; i < 32; i++) {
        if (!(slots & (1U << i))) return i;
    }
    return -1;
}

typedef struct __attribute__((packed)) tagFIS_REG_H2D {
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

    UINT64 rflags = 0;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(rflags) :: "memory");

    if (!(port->cmd & PORT_CMD_ST)) {
        start_cmd(port);
    }

    int mem_slot = get_mem_slot_from_port(port);
    if (mem_slot < 0) {
        if (rflags & 0x200) __asm__ volatile("sti");
        return 0;
    }

    int slot = find_cmd_slot(port);
    if (slot < 0) {
        if (rflags & 0x200) __asm__ volatile("sti");
        return 0;
    }

    HBA_CMD_HEADER *cmd_header = &g_cmd_lists[mem_slot][slot];
    HBA_CMD_TABLE *cmd_table = &g_cmd_tables[mem_slot][slot];

    __builtin_memset(cmd_header, 0, sizeof(HBA_CMD_HEADER));
    __builtin_memset(cmd_table, 0, sizeof(HBA_CMD_TABLE));

    UINT32 ctba_low, ctba_high;
    split_u64((UINT64)(UINTN)cmd_table, &ctba_low, &ctba_high);

    cmd_header->cfl = 5;
    cmd_header->w = write ? 1 : 0;
    cmd_header->prdtl = 1;
    cmd_header->ctba = ctba_low;
    cmd_header->ctbau = ctba_high;

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
    fis->device = 0x40;

    fis->lba0 = (UINT8)(start_lba_low & 0xFF);
    fis->lba1 = (UINT8)((start_lba_low >> 8) & 0xFF);
    fis->lba2 = (UINT8)((start_lba_low >> 16) & 0xFF);
    fis->lba3 = (UINT8)((start_lba_low >> 24) & 0xFF);
    fis->lba4 = (UINT8)(start_lba_high & 0xFF);
    fis->lba5 = (UINT8)((start_lba_high >> 8) & 0xFF);
    fis->countl = (UINT8)(sector_count & 0xFF);
    fis->counth = (UINT8)((sector_count >> 8) & 0xFF);

    port->is = 0xFFFFFFFFU;
    UINT32 slot_mask = 1U << slot;
    port->ci = slot_mask;

    int success = 0;
    for (UINT32 i = 0; i < AHCI_TIMEOUT; i++) {
        if (port->is & PORT_IS_TFES) {
            port->ci &= ~slot_mask;
            success = 0;
            break;
        }
        if (!(port->ci & slot_mask)) {
            success = (port->is & PORT_IS_TFES) ? 0 : 1;
            break;
        }
        io_wait();
    }

    port->ci &= ~slot_mask;
    if (rflags & 0x200) __asm__ volatile("sti");
    return success;
}

int read_sata_sector(void *port,
                     UINT32 start_lba_low,
                     UINT32 start_lba_high,
                     UINT32 sector_count,
                     void *buf) {
    for (int i = 0; i < g_port_count; i++) {
        if (g_ports[i].port_addr == port && g_ports[i].is_nvme) {
            return nvme_read_sector(port, start_lba_low, start_lba_high, sector_count, buf);
        }
    }
    return sata_transfer((HBA_PORT *)port, start_lba_low, start_lba_high, sector_count, buf, 0);
}

int write_sata_sector(void *port,
                      UINT32 start_lba_low,
                      UINT32 start_lba_high,
                      UINT32 sector_count,
                      void *buf) {
    for (int i = 0; i < g_port_count; i++) {
        if (g_ports[i].port_addr == port && g_ports[i].is_nvme) {
            return nvme_write_sector(port, start_lba_low, start_lba_high, sector_count, buf);
        }
    }
    return sata_transfer((HBA_PORT *)port, start_lba_low, start_lba_high, sector_count, buf, 1);
}

static int ahci_identify_device(HBA_PORT *port, int mem_slot) {
    if (!port || mem_slot < 0 || mem_slot >= 32) return 0;
    
    for (int wait = 0; wait < 2000; wait++) {
        if ((port->tfd & 0x88) == 0) break;
        io_wait();
    }

    int slot = find_cmd_slot(port);
    if (slot < 0) return 0;

    __builtin_memset(g_identify_buffer[mem_slot], 0, 512);

    HBA_CMD_HEADER *cmd_header = &g_cmd_lists[mem_slot][slot];
    HBA_CMD_TABLE *cmd_table = &g_cmd_tables[mem_slot][slot];

    __builtin_memset(cmd_header, 0, sizeof(HBA_CMD_HEADER));
    __builtin_memset(cmd_table, 0, sizeof(HBA_CMD_TABLE));

    UINT32 ctba_low, ctba_high;
    split_u64((UINT64)(UINTN)cmd_table, &ctba_low, &ctba_high);

    cmd_header->cfl = 5;
    cmd_header->w = 0;
    cmd_header->prdtl = 1;
    cmd_header->ctba = ctba_low;
    cmd_header->ctbau = ctba_high;

    UINT64 buffer_addr = (UINT64)(UINTN)&g_identify_buffer[mem_slot][0];
    UINT32 buf_low, buf_high;
    split_u64(buffer_addr, &buf_low, &buf_high);

    cmd_table->prdt_entry[0].dba = buf_low;
    cmd_table->prdt_entry[0].dbau = buf_high;
    cmd_table->prdt_entry[0].dbc = 511;

    FIS_REG_H2D *fis = (FIS_REG_H2D *)(UINTN)&cmd_table->cfis[0];
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = ATA_CMD_IDENTIFY;

    port->is = 0xFFFFFFFFU;
    UINT32 slot_mask = 1U << slot;
    port->ci = slot_mask;

    for (UINT32 i = 0; i < AHCI_TIMEOUT; i++) {
        if (port->is & PORT_IS_TFES) {
            port->ci &= ~slot_mask;
            return 0;
        }
        if (!(port->ci & slot_mask)) break;
        io_wait();
    }

    if (port->ci & slot_mask) {
        port->ci &= ~slot_mask;
        return 0;
    }
    return 1;
}

int ahci_identify_drive(void *port, int mem_slot, AHCI_PORT_INFO *info) {
    if (!port || !info) return 0;
    __builtin_memset(info, 0, sizeof(AHCI_PORT_INFO));
    
    if (!ahci_identify_device((HBA_PORT*)port, mem_slot)) {
        return 0;
    }
    
    UINT16 *data = g_identify_buffer[mem_slot];
    UINT32 lba48_low = ((UINT32)data[100]) | ((UINT32)data[101] << 16);
    UINT32 lba48_high = ((UINT32)data[102]) | ((UINT32)data[103] << 16);
    info->sector_count = ((UINT64)lba48_high << 32) | lba48_low;
    
    if (info->sector_count == 0) {
        UINT32 lba28 = ((UINT32)data[60]) | ((UINT32)data[61] << 16);
        info->sector_count = lba28;
    }
    
    for (int i = 0; i < 20; i++) {
        UINT16 w = data[27 + i];
        info->model[i*2] = (w >> 8) & 0xFF;
        info->model[i*2 + 1] = w & 0xFF;
    }
    info->model[40] = '\0';
    return 1;
}

static void probe_hba(HBA_MEM *hba) {
    if (!hba) return;

    hba->ghc |= (1U << 31);
    for (int wait = 0; wait < 1000; wait++) {
        if (hba->ghc & (1U << 31)) break;
        io_wait();
    }

    UINT32 ports_implemented = hba->pi;
    if (ports_implemented == 0 || ports_implemented == 0xFFFFFFFFU) {
        ports_implemented = 0x01;
    }

    for (int i = 0; i < 32 && g_port_count < MAX_AHCI_PORTS; i++) {
        if (!(ports_implemented & (1U << i))) continue;

        HBA_PORT *port = &hba->ports[i];
        
        UINT32 ssts = port->ssts;
        UINT8 det = (UINT8)(ssts & 0x0F);
        if (det == 0 && port->sig != 0x00000101U) {
            continue;
        }

        int mem_slot = g_port_count;
        init_port_memory(port, mem_slot);

        port->cmd |= (PORT_CMD_SUD | PORT_CMD_POD);
        port->cmd = (port->cmd & ~0xF0000000U) | 0x10000000U;

        int link_up = 0;
        for (int w = 0; w < 5000; w++) {
            ssts = port->ssts;
            det = (UINT8)(ssts & 0x0F);
            if (det == 3 || port->sig == 0x00000101U) {
                link_up = 1;
                break;
            }
            io_wait();
        }

        AHCI_PORT_INFO temp_info;
        __builtin_memset(&temp_info, 0, sizeof(AHCI_PORT_INFO));
        int identified = ahci_identify_drive((void*)(UINTN)port, mem_slot, &temp_info);

        if (link_up || identified) {
            g_ports[g_port_count].port_addr = (void*)(UINTN)port;
            g_ports[g_port_count].port_number = i;
            g_ports[g_port_count].active = 1;
            g_ports[g_port_count].is_nvme = 0;

            if (identified && temp_info.sector_count > 0) {
                g_ports[g_port_count].sector_count = temp_info.sector_count;
                __builtin_memcpy(g_ports[g_port_count].model, temp_info.model, 41);
            } else {
                g_ports[g_port_count].sector_count = 131072;
                __builtin_memcpy(g_ports[g_port_count].model, "SATA Drive", 11);
            }
            g_port_count++;
        } else {
            stop_cmd(port);
        }
    }
}

void init_ahci(UINTN abar_address) {
    g_port_count = 0;
    if (abar_address != 0) {
        probe_hba((HBA_MEM *)(UINTN)abar_address);
    }

    for (UINT16 bus = 0; bus < 256; bus++) {
        for (UINT8 slot = 0; slot < 32; slot++) {
            for (UINT8 func = 0; func < 8; func++) {
                UINT32 vendor_device = pci_config_read((UINT8)bus, slot, func, 0x00);
                if ((vendor_device & 0xFFFFU) == 0xFFFFU) {
                    if (func == 0) break;
                    continue;
                }

                UINT32 class_code = pci_config_read((UINT8)bus, slot, func, 0x08);
                UINT8 base_class = (UINT8)((class_code >> 24) & 0xFF);
                UINT8 sub_class  = (UINT8)((class_code >> 16) & 0xFF);

                int is_match = (base_class == 0x01 && sub_class == 0x06) || 
                               (vendor_device == 0x29228086U) || 
                               ((vendor_device & 0xFFFFU) == 0x8086U && sub_class == 0x06);
                if (!is_match) continue;

                pci_config_write((UINT8)bus, slot, func, 0x04, 0x0007);

                UINT32 bar5_low = pci_config_read((UINT8)bus, slot, func, 0x24);
                UINT64 abar = (UINT64)(bar5_low & 0xFFFFFFF0U);
                if ((bar5_low & 0x6U) == 0x4U) {
                    UINT32 bar5_high = pci_config_read((UINT8)bus, slot, func, 0x28);
                    abar |= ((UINT64)bar5_high << 32);
                }

                if (abar == 0) continue;
                probe_hba((HBA_MEM *)(UINTN)abar);
                if (g_port_count > 0) return;
            }
        }
    }

    // Wenn keine SATA-Laufwerke gefunden wurden: PCIe NVMe als automatischer Fallback
    if (g_port_count == 0) {
        if (init_nvme()) {
            NVME_DEVICE_INFO *ndev = nvme_get_device_info(0);
            if (ndev && ndev->active) {
                g_ports[0].port_addr = nvme_get_device(0);
                g_ports[0].port_number = 0;
                g_ports[0].active = 1;
                g_ports[0].sector_count = ndev->sector_count;
                g_ports[0].is_nvme = 1;
                __builtin_memcpy(g_ports[0].model, ndev->model, 41);
                g_port_count = 1;
            }
        }
    }
}

int ahci_get_port_count(void) {
    return g_port_count;
}

AHCI_PORT_INFO* ahci_get_port_info(int index) {
    if (index < 0 || index >= g_port_count) return NULL;
    return &g_ports[index];
}

void* ahci_get_port(int index) {
    if (index < 0 || index >= g_port_count) return NULL;
    return g_ports[index].port_addr;
}
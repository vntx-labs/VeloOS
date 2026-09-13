#include "nvme.h"

void klog(const char *s);

#define NVME_REG_CAP     0x0000
#define NVME_REG_VS      0x0008
#define NVME_REG_INTMS   0x000C
#define NVME_REG_INTMC   0x0010
#define NVME_REG_CC      0x0014
#define NVME_REG_CSTS    0x001C
#define NVME_REG_AQA     0x0024
#define NVME_REG_ASQ     0x0028
#define NVME_REG_ACQ     0x0030

#define NVME_ADMIN_SUBMISSION_QUEUE_SIZE 64
#define NVME_ADMIN_COMPLETION_QUEUE_SIZE 64
#define NVME_IO_SUBMISSION_QUEUE_SIZE    64
#define NVME_IO_COMPLETION_QUEUE_SIZE    64

#define NVME_ADMIN_CMD_DELETE_IO_SQ 0x00
#define NVME_ADMIN_CMD_CREATE_IO_SQ 0x01
#define NVME_ADMIN_CMD_DELETE_IO_CQ 0x04
#define NVME_ADMIN_CMD_CREATE_IO_CQ 0x05
#define NVME_ADMIN_CMD_IDENTIFY     0x06
#define NVME_NVM_CMD_WRITE          0x01
#define NVME_NVM_CMD_READ           0x02

typedef struct __attribute__((packed)) {
    UINT8  opcode;
    UINT8  flags;
    UINT16 command_id;
    UINT32 nsid;
    UINT64 rsvd1;
    UINT64 mptr;
    UINT64 prp1;
    UINT64 prp2;
    UINT32 cdw10;
    UINT32 cdw11;
    UINT32 cdw12;
    UINT32 cdw13;
    UINT32 cdw14;
    UINT32 cdw15;
} NVME_COMMAND;

typedef struct __attribute__((packed)) {
    UINT32 dw0;
    UINT32 dw1;
    UINT16 sq_head;
    UINT16 sq_id;
    UINT16 command_id;
    UINT16 status;
} NVME_COMPLETION;

typedef struct {
    UINTN bar0;
    UINT32 dstrd;
    UINT32 nsid;
    UINT64 sector_count;
    UINT32 sector_size;
    char   model[41];
    int    active;

    NVME_COMMAND    *asq;
    NVME_COMPLETION *acq;
    UINT16 asq_tail;
    UINT16 acq_head;
    UINT8  acq_phase;
    UINT16 admin_cmd_id;

    NVME_COMMAND    *iosq;
    NVME_COMPLETION *iocq;
    UINT16 iosq_tail;
    UINT16 iocq_head;
    UINT8  iocq_phase;
    UINT16 io_cmd_id;
} NVME_CONTROLLER;

static NVME_CONTROLLER g_nvme_ctrls[MAX_NVME_DEVICES] = {0};
static NVME_DEVICE_INFO g_nvme_dev_infos[MAX_NVME_DEVICES] = {0};
static int g_nvme_count = 0;

static NVME_COMMAND    g_asq_buf[MAX_NVME_DEVICES][NVME_ADMIN_SUBMISSION_QUEUE_SIZE] __attribute__((aligned(4096)));
static NVME_COMPLETION g_acq_buf[MAX_NVME_DEVICES][NVME_ADMIN_COMPLETION_QUEUE_SIZE] __attribute__((aligned(4096)));
static NVME_COMMAND    g_iosq_buf[MAX_NVME_DEVICES][NVME_IO_SUBMISSION_QUEUE_SIZE]   __attribute__((aligned(4096)));
static NVME_COMPLETION g_iocq_buf[MAX_NVME_DEVICES][NVME_IO_COMPLETION_QUEUE_SIZE]  __attribute__((aligned(4096)));
static UINT8           g_identify_buf[4096]                                          __attribute__((aligned(4096)));
static UINT8           g_nvme_io_dma[4096]                                           __attribute__((aligned(4096)));

static inline void outl_pci(unsigned short port, unsigned int val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline unsigned int inl_pci(unsigned short port) {
    unsigned int ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void outb_io(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline void nvme_io_wait(void) {
    outb_io(0x80, 0);
}

static UINT32 pci_cfg_read32(UINT8 bus, UINT8 slot, UINT8 func, UINT8 offset) {
    UINT32 address = (1U << 31) | ((UINT32)bus << 16) | ((UINT32)(slot & 0x1F) << 11) | ((UINT32)(func & 0x07) << 8) | (UINT32)(offset & 0xFC);
    outl_pci(0xCF8, address);
    return inl_pci(0xCFC);
}

static void pci_cfg_write32(UINT8 bus, UINT8 slot, UINT8 func, UINT8 offset, UINT32 val) {
    UINT32 address = (1U << 31) | ((UINT32)bus << 16) | ((UINT32)(slot & 0x1F) << 11) | ((UINT32)(func & 0x07) << 8) | (UINT32)(offset & 0xFC);
    outl_pci(0xCF8, address);
    outl_pci(0xCFC, val);
}

static inline UINT32 nvme_read32(NVME_CONTROLLER *ctrl, UINT32 reg) {
    return *(volatile UINT32*)(ctrl->bar0 + reg);
}

static inline void nvme_write32(NVME_CONTROLLER *ctrl, UINT32 reg, UINT32 val) {
    *(volatile UINT32*)(ctrl->bar0 + reg) = val;
}

static inline void nvme_write64(NVME_CONTROLLER *ctrl, UINT32 reg, UINT64 val) {
    *(volatile UINT32*)(ctrl->bar0 + reg) = (UINT32)(val & 0xFFFFFFFF);
    *(volatile UINT32*)(ctrl->bar0 + reg + 4) = (UINT32)(val >> 32);
}

static inline void nvme_ring_doorbell(NVME_CONTROLLER *ctrl, int qid, int is_cq, UINT32 val) {
    UINT32 dbl_offset = 0x1000 + (2 * qid + (is_cq ? 1 : 0)) * (4 << ctrl->dstrd);
    nvme_write32(ctrl, dbl_offset, val);
}

static int nvme_submit_admin_cmd(NVME_CONTROLLER *ctrl, NVME_COMMAND *cmd, NVME_COMPLETION *cpl_out) {
    cmd->command_id = ++ctrl->admin_cmd_id;
    __builtin_memcpy(&ctrl->asq[ctrl->asq_tail], cmd, sizeof(NVME_COMMAND));

    ctrl->asq_tail = (ctrl->asq_tail + 1) % NVME_ADMIN_SUBMISSION_QUEUE_SIZE;
    nvme_ring_doorbell(ctrl, 0, 0, ctrl->asq_tail);

    int timeout = 500000;
    while (timeout--) {
        volatile NVME_COMPLETION *cpl = &ctrl->acq[ctrl->acq_head];
        UINT8 phase = (cpl->status & 1);
        if (phase == ctrl->acq_phase) {
            if (cpl_out) __builtin_memcpy(cpl_out, (void*)cpl, sizeof(NVME_COMPLETION));
            ctrl->acq_head = (ctrl->acq_head + 1) % NVME_ADMIN_COMPLETION_QUEUE_SIZE;
            if (ctrl->acq_head == 0) ctrl->acq_phase ^= 1;
            nvme_ring_doorbell(ctrl, 0, 1, ctrl->acq_head);
            return (cpl->status >> 1) == 0;
        }
        nvme_io_wait();
    }
    return 0;
}

static int nvme_submit_io_cmd(NVME_CONTROLLER *ctrl, NVME_COMMAND *cmd, NVME_COMPLETION *cpl_out) {
    cmd->command_id = ++ctrl->io_cmd_id;
    __builtin_memcpy(&ctrl->iosq[ctrl->iosq_tail], cmd, sizeof(NVME_COMMAND));

    ctrl->iosq_tail = (ctrl->iosq_tail + 1) % NVME_IO_SUBMISSION_QUEUE_SIZE;
    nvme_ring_doorbell(ctrl, 1, 0, ctrl->iosq_tail);

    int timeout = 500000;
    while (timeout--) {
        volatile NVME_COMPLETION *cpl = &ctrl->iocq[ctrl->iocq_head];
        UINT8 phase = (cpl->status & 1);
        if (phase == ctrl->iocq_phase) {
            if (cpl_out) __builtin_memcpy(cpl_out, (void*)cpl, sizeof(NVME_COMPLETION));
            ctrl->iocq_head = (ctrl->iocq_head + 1) % NVME_IO_COMPLETION_QUEUE_SIZE;
            if (ctrl->iocq_head == 0) ctrl->iocq_phase ^= 1;
            nvme_ring_doorbell(ctrl, 1, 1, ctrl->iocq_head);
            return (cpl->status >> 1) == 0;
        }
        nvme_io_wait();
    }
    return 0;
}

static int init_controller(NVME_CONTROLLER *ctrl, int slot_idx) {
    UINT32 cap_high = nvme_read32(ctrl, NVME_REG_CAP + 4);
    ctrl->dstrd = (cap_high >> 0) & 0xF;

    UINT32 cc = nvme_read32(ctrl, NVME_REG_CC);
    if (cc & 1) {
        nvme_write32(ctrl, NVME_REG_CC, cc & ~1);
        int t = 500000;
        while ((nvme_read32(ctrl, NVME_REG_CSTS) & 1) && --t) {
            nvme_io_wait();
        }
    }

    ctrl->asq = g_asq_buf[slot_idx];
    ctrl->acq = g_acq_buf[slot_idx];
    ctrl->asq_tail = 0;
    ctrl->acq_head = 0;
    ctrl->acq_phase = 1;
    ctrl->admin_cmd_id = 0;
    __builtin_memset(ctrl->asq, 0, sizeof(g_asq_buf[slot_idx]));
    __builtin_memset(ctrl->acq, 0, sizeof(g_acq_buf[slot_idx]));

    UINT32 aqa = ((NVME_ADMIN_COMPLETION_QUEUE_SIZE - 1) << 16) | (NVME_ADMIN_SUBMISSION_QUEUE_SIZE - 1);
    nvme_write32(ctrl, NVME_REG_AQA, aqa);
    nvme_write64(ctrl, NVME_REG_ASQ, (UINT64)(UINTN)ctrl->asq);
    nvme_write64(ctrl, NVME_REG_ACQ, (UINT64)(UINTN)ctrl->acq);

    cc = (6 << 16) | (4 << 20) | (0 << 7) | (0 << 4) | 1;
    nvme_write32(ctrl, NVME_REG_CC, cc);

    int t = 500000;
    while (!(nvme_read32(ctrl, NVME_REG_CSTS) & 1) && --t) {
        nvme_io_wait();
    }
    if (!t) return 0;

    // FEHLER BEHOBEN: Puffer VOR dem Absenden des Admin-Befehls zuweisen!
    ctrl->iocq = g_iocq_buf[slot_idx];
    ctrl->iocq_head = 0;
    ctrl->iocq_phase = 1;
    ctrl->io_cmd_id = 0;
    __builtin_memset(ctrl->iocq, 0, sizeof(g_iocq_buf[slot_idx]));

    NVME_COMMAND cmd;
    __builtin_memset(&cmd, 0, sizeof(NVME_COMMAND));
    cmd.opcode = NVME_ADMIN_CMD_CREATE_IO_CQ;
    cmd.prp1 = (UINT64)(UINTN)ctrl->iocq;
    cmd.cdw10 = ((NVME_IO_COMPLETION_QUEUE_SIZE - 1) << 16) | 1;
    cmd.cdw11 = 1;
    if (!nvme_submit_admin_cmd(ctrl, &cmd, NULL)) return 0;

    ctrl->iosq = g_iosq_buf[slot_idx];
    ctrl->iosq_tail = 0;
    __builtin_memset(ctrl->iosq, 0, sizeof(g_iosq_buf[slot_idx]));

    __builtin_memset(&cmd, 0, sizeof(NVME_COMMAND));
    cmd.opcode = NVME_ADMIN_CMD_CREATE_IO_SQ;
    cmd.prp1 = (UINT64)(UINTN)ctrl->iosq;
    cmd.cdw10 = ((NVME_IO_SUBMISSION_QUEUE_SIZE - 1) << 16) | 1;
    cmd.cdw11 = (1 << 16) | 1;
    if (!nvme_submit_admin_cmd(ctrl, &cmd, NULL)) return 0;

    __builtin_memset(g_identify_buf, 0, 4096);
    __builtin_memset(&cmd, 0, sizeof(NVME_COMMAND));
    cmd.opcode = NVME_ADMIN_CMD_IDENTIFY;
    cmd.prp1 = (UINT64)(UINTN)g_identify_buf;
    cmd.cdw10 = 1;
    if (nvme_submit_admin_cmd(ctrl, &cmd, NULL)) {
        for (int i = 0; i < 40; i++) {
            ctrl->model[i] = g_identify_buf[24 + i];
        }
        ctrl->model[40] = '\0';
    } else {
        __builtin_memcpy(ctrl->model, "NVMe PCIe SSD", 14);
    }

    __builtin_memset(g_identify_buf, 0, 4096);
    __builtin_memset(&cmd, 0, sizeof(NVME_COMMAND));
    cmd.opcode = NVME_ADMIN_CMD_IDENTIFY;
    cmd.nsid = 1;
    cmd.prp1 = (UINT64)(UINTN)g_identify_buf;
    cmd.cdw10 = 0;
    if (nvme_submit_admin_cmd(ctrl, &cmd, NULL)) {
        ctrl->nsid = 1;
        ctrl->sector_count = *(UINT64*)&g_identify_buf[0];
        UINT8 flbas = g_identify_buf[26] & 0x0F;
        UINT8 lbads = g_identify_buf[128 + flbas * 4 + 2];
        ctrl->sector_size = (lbads > 0) ? (1U << lbads) : 512;
    } else {
        ctrl->nsid = 1;
        ctrl->sector_count = 4194304ULL;
        ctrl->sector_size = 512;
    }

    ctrl->active = 1;
    return 1;
}

int init_nvme(void) {
    g_nvme_count = 0;
    __builtin_memset(g_nvme_ctrls, 0, sizeof(g_nvme_ctrls));
    __builtin_memset(g_nvme_dev_infos, 0, sizeof(g_nvme_dev_infos));

    for (UINT16 bus = 0; bus < 256; bus++) {
        for (UINT8 slot = 0; slot < 32; slot++) {
            for (UINT8 func = 0; func < 8; func++) {
                if (g_nvme_count >= MAX_NVME_DEVICES) return g_nvme_count;

                UINT32 ven_dev = pci_cfg_read32((UINT8)bus, slot, func, 0x00);
                if ((ven_dev & 0xFFFFU) == 0xFFFFU) {
                    if (func == 0) break;
                    continue;
                }

                UINT32 class_rev = pci_cfg_read32((UINT8)bus, slot, func, 0x08);
                UINT8 base_class = (UINT8)(class_rev >> 24);
                UINT8 sub_class  = (UINT8)(class_rev >> 16);

                if (base_class == 0x01 && sub_class == 0x08) {
                    pci_cfg_write32((UINT8)bus, slot, func, 0x04, 0x0007);

                    UINT32 bar0_low = pci_cfg_read32((UINT8)bus, slot, func, 0x10);
                    UINT64 bar0 = (UINT64)(bar0_low & 0xFFFFFFF0U);
                    if ((bar0_low & 0x6U) == 0x4U) {
                        UINT32 bar0_high = pci_cfg_read32((UINT8)bus, slot, func, 0x14);
                        bar0 |= ((UINT64)bar0_high << 32);
                    }

                    if (bar0 == 0) continue;

                    int idx = g_nvme_count;
                    g_nvme_ctrls[idx].bar0 = (UINTN)bar0;
                    if (init_controller(&g_nvme_ctrls[idx], idx)) {
                        g_nvme_dev_infos[idx].bar0 = g_nvme_ctrls[idx].bar0;
                        g_nvme_dev_infos[idx].nsid = g_nvme_ctrls[idx].nsid;
                        g_nvme_dev_infos[idx].sector_count = g_nvme_ctrls[idx].sector_count;
                        g_nvme_dev_infos[idx].sector_size = g_nvme_ctrls[idx].sector_size;
                        __builtin_memcpy(g_nvme_dev_infos[idx].model, g_nvme_ctrls[idx].model, 41);
                        g_nvme_dev_infos[idx].active = 1;
                        g_nvme_count++;
                    }
                }
            }
        }
    }
    return g_nvme_count;
}

int nvme_get_device_count(void) { return g_nvme_count; }

NVME_DEVICE_INFO* nvme_get_device_info(int index) {
    if (index < 0 || index >= g_nvme_count) return NULL;
    return &g_nvme_dev_infos[index];
}

void* nvme_get_device(int index) {
    if (index < 0 || index >= g_nvme_count) return NULL;
    return (void*)&g_nvme_ctrls[index];
}

int nvme_read_sector(void *dev, UINT32 start_lba_low, UINT32 start_lba_high, UINT32 sector_count, void *buf) {
    NVME_CONTROLLER *ctrl = (NVME_CONTROLLER*)dev;
    if (!ctrl || !ctrl->active || !buf || sector_count == 0) return 0;

    UINT8 *ptr = (UINT8*)buf;
    UINT32 cur_lba = start_lba_low;

    while (sector_count > 0) {
        UINT32 count = (sector_count > 8) ? 8 : sector_count;

        NVME_COMMAND cmd;
        __builtin_memset(&cmd, 0, sizeof(NVME_COMMAND));
        cmd.opcode = NVME_NVM_CMD_READ;
        cmd.nsid = ctrl->nsid;
        cmd.prp1 = (UINT64)(UINTN)g_nvme_io_dma;
        cmd.cdw10 = cur_lba;
        cmd.cdw11 = start_lba_high;
        cmd.cdw12 = (count - 1) & 0xFFFF;

        if (!nvme_submit_io_cmd(ctrl, &cmd, NULL)) return 0;

        __builtin_memcpy(ptr, g_nvme_io_dma, count * 512);

        sector_count -= count;
        cur_lba += count;
        ptr += count * 512;
    }
    return 1;
}

int nvme_write_sector(void *dev, UINT32 start_lba_low, UINT32 start_lba_high, UINT32 sector_count, void *buf) {
    NVME_CONTROLLER *ctrl = (NVME_CONTROLLER*)dev;
    if (!ctrl || !ctrl->active || !buf || sector_count == 0) return 0;

    UINT8 *ptr = (UINT8*)buf;
    UINT32 cur_lba = start_lba_low;

    while (sector_count > 0) {
        UINT32 count = (sector_count > 8) ? 8 : sector_count;
        __builtin_memcpy(g_nvme_io_dma, ptr, count * 512);

        NVME_COMMAND cmd;
        __builtin_memset(&cmd, 0, sizeof(NVME_COMMAND));
        cmd.opcode = NVME_NVM_CMD_WRITE;
        cmd.nsid = ctrl->nsid;
        cmd.prp1 = (UINT64)(UINTN)g_nvme_io_dma;
        cmd.cdw10 = cur_lba;
        cmd.cdw11 = start_lba_high;
        cmd.cdw12 = (count - 1) & 0xFFFF;

        if (!nvme_submit_io_cmd(ctrl, &cmd, NULL)) return 0;

        sector_count -= count;
        cur_lba += count;
        ptr += count * 512;
    }
    return 1;
}
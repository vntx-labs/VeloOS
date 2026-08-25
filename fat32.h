#ifndef FAT32_H
#define FAT32_H

#include <efi.h>
#include <efilib.h>

typedef struct {
    char   name[32];
    UINT32 size;
    UINT16 date;
    UINT16 time;
    UINT8  is_dir;
    UINT8  attr;
} VeloDirEntry;

typedef struct {
    UINT8  jump_boot[3];
    UINT8  oem_name[8];
    UINT16 bytes_per_sector;
    UINT8  sectors_per_cluster;
    UINT16 reserved_sector_count;
    UINT8  num_fats;
    UINT16 root_entry_count;
    UINT16 total_sectors_16;
    UINT8  media;
    UINT16 fat_size_16;
    UINT16 sectors_per_track;
    UINT16 head_count;
    UINT32 hidden_sectors;
    UINT32 total_sectors_32;
    UINT32 table_size_32;
    UINT16 ext_flags;
    UINT16 fs_version;
    UINT32 root_cluster;
    UINT16 fs_info;
    UINT16 backup_boot_sector;
    UINT8  reserved[12];
    UINT8  drive_number;
    UINT8  reserved1;
    UINT8  boot_signature;
    UINT32 volume_id;
    UINT8  volume_label[11];
    UINT8  fs_type[8];
} __attribute__((packed)) FAT32_BPB;

typedef struct {
    UINT8  name[11];
    UINT8  attr;
    UINT8  nt_res;
    UINT8  crt_time_tenth;
    UINT16 crt_time;
    UINT16 crt_date;
    UINT16 lst_acc_date;
    UINT16 first_cluster_high;
    UINT16 wrt_time;
    UINT16 wrt_date;
    UINT16 first_cluster_low;
    UINT32 file_size;
} __attribute__((packed)) FAT32_DIR_ENTRY;

int read_sata_sector(void *port, UINT32 start_lba_low, UINT32 start_lba_high, UINT32 sector_count, void *buf);
int write_sata_sector(void *port, UINT32 start_lba_low, UINT32 start_lba_high, UINT32 sector_count, void *buf);

int fat32_init(void *ahci_port);
int fat32_format(void *ahci_port, UINT64 total_sectors);
int fat32_mkdir(void *ahci_port, const char *path);
int fat32_read_file(void *ahci_port, const char *filename, void *buffer, UINT32 max_size);
int fat32_write_file(void *ahci_port, const char *filename, void *buffer, UINT32 size);
int fat32_list_dir(void *ahci_port, const char *path, VeloDirEntry *out_entries, int max_entries);
int fat32_list_root(void *ahci_port, char out_files[][32], int max_files);
UINT64 fat32_get_free_bytes(void *ahci_port);

#endif
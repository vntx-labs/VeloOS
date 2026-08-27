#include "fat32.h"
#include "ahci.h"

#define MAX_VOLUMES 8
#define FAT_EOC           0x0FFFFFF8U
#define FAT_BAD           0x0FFFFFF7U
#define FAT_FREE          0x00000000U
#define FAT32_MAX_CLUSTER 0x0FFFFFF6U

typedef struct {
    void *port;
    UINT32 lba_offset;
    FAT32_BPB bpb;
    UINT32 first_data_sector;
    UINT32 bytes_per_cluster;
    UINT32 total_clusters;
    UINT64 cached_free_bytes;
    UINT32 last_free_cluster_hint;
    int initialized;
} FAT32_VOLUME;

static FAT32_VOLUME g_volumes[MAX_VOLUMES] = {0};
static UINT8 g_fat32_io_buf[131072] __attribute__((aligned(4096)));

static inline int kstrcmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return -1;
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static UINT32 read_u32_le(const UINT8 *p) {
    return ((UINT32)p[0]) |
           ((UINT32)p[1] << 8) |
           ((UINT32)p[2] << 16) |
           ((UINT32)p[3] << 24);
}

static void write_u32_le(UINT8 *p, UINT32 value) {
    p[0] = (UINT8)(value & 0xFF);
    p[1] = (UINT8)((value >> 8) & 0xFF);
    p[2] = (UINT8)((value >> 16) & 0xFF);
    p[3] = (UINT8)((value >> 24) & 0xFF);
}

static FAT32_VOLUME* get_volume(void *port) {
    if (!port) return NULL;
    for (int i = 0; i < MAX_VOLUMES; i++) {
        if (g_volumes[i].port == port && g_volumes[i].initialized) {
            return &g_volumes[i];
        }
    }
    for (int i = 0; i < MAX_VOLUMES; i++) {
        if (g_volumes[i].port == port) {
            if (fat32_init(port)) return &g_volumes[i];
            return NULL;
        }
    }
    for (int i = 0; i < MAX_VOLUMES; i++) {
        if (g_volumes[i].port == NULL) {
            g_volumes[i].port = port;
            if (fat32_init(port)) return &g_volumes[i];
            return NULL;
        }
    }
    return NULL;
}

static UINT32 cluster_to_lba(FAT32_VOLUME *vol, UINT32 cluster) {
    if (cluster < 2 || cluster >= vol->total_clusters + 2) return 0;
    return vol->first_data_sector + (cluster - 2U) * (UINT32)vol->bpb.sectors_per_cluster;
}

static int valid_cluster(FAT32_VOLUME *vol, UINT32 cluster) {
    return cluster >= 2 && cluster < vol->total_clusters + 2;
}

static int is_eoc(UINT32 cluster) {
    return cluster >= FAT_EOC;
}

static UINT32 fat_sector_for_cluster(FAT32_VOLUME *vol, UINT32 cluster) {
    return vol->lba_offset + vol->bpb.reserved_sector_count + ((cluster * 4U) / vol->bpb.bytes_per_sector);
}

static UINT32 fat_offset_in_sector(FAT32_VOLUME *vol, UINT32 cluster) {
    return (cluster * 4U) % vol->bpb.bytes_per_sector;
}

int fat32_format(void *ahci_port, UINT64 total_sectors) {
    if (!ahci_port || total_sectors < 65536) return 0;

    UINT8 sector_buf[512] __attribute__((aligned(16)));
    __builtin_memset(sector_buf, 0, 512);

    FAT32_BPB *bpb = (FAT32_BPB *)sector_buf;
    bpb->jump_boot[0] = 0xEB; bpb->jump_boot[1] = 0x58; bpb->jump_boot[2] = 0x90;
    __builtin_memcpy(bpb->oem_name, "MSWIN4.1", 8);
    bpb->bytes_per_sector = 512;
    bpb->sectors_per_cluster = (total_sectors > 67108864ULL) ? 64 : ((total_sectors > 16777216ULL) ? 32 : 8);
    bpb->reserved_sector_count = 32;
    bpb->num_fats = 2;
    bpb->media = 0xF8;
    bpb->sectors_per_track = 63;
    bpb->head_count = 255;
    bpb->total_sectors_32 = (UINT32)total_sectors;

    UINT32 data_sec = bpb->total_sectors_32 - bpb->reserved_sector_count;
    UINT32 fat_sz = ((data_sec / bpb->sectors_per_cluster) * 4 + 511) / 512;
    bpb->table_size_32 = fat_sz;
    bpb->root_cluster = 2;
    bpb->fs_info = 1;
    bpb->backup_boot_sector = 6;
    bpb->boot_signature = 0x29;
    bpb->volume_id = 0x12345678;
    __builtin_memcpy(bpb->volume_label, "VELO_DISK  ", 11);
    __builtin_memcpy(bpb->fs_type, "FAT32   ", 8);
    sector_buf[510] = 0x55; sector_buf[511] = 0xAA;

    write_sata_sector(ahci_port, 0, 0, 1, sector_buf);

    __builtin_memset(sector_buf, 0, 512);
    sector_buf[0] = 0x52; sector_buf[1] = 0x52; sector_buf[2] = 0x61; sector_buf[3] = 0x41;
    sector_buf[484] = 0x72; sector_buf[485] = 0x72; sector_buf[486] = 0x41; sector_buf[487] = 0x61;
    write_u32_le(&sector_buf[488], (data_sec / bpb->sectors_per_cluster) - 1);
    write_u32_le(&sector_buf[492], 3);
    sector_buf[510] = 0x55; sector_buf[511] = 0xAA;
    write_sata_sector(ahci_port, 1, 0, 1, sector_buf);

    __builtin_memset(sector_buf, 0, 512);
    write_u32_le(&sector_buf[0], 0x0FFFFFF8);
    write_u32_le(&sector_buf[4], 0x0FFFFFFF);
    write_u32_le(&sector_buf[8], 0x0FFFFFFF);

    write_sata_sector(ahci_port, bpb->reserved_sector_count, 0, 1, sector_buf);
    write_sata_sector(ahci_port, bpb->reserved_sector_count + fat_sz, 0, 1, sector_buf);

    return fat32_init(ahci_port);
}

int fat32_init(void *ahci_port) {
    if (!ahci_port) return 0;
    UINT8 sector_buf[512] __attribute__((aligned(16)));

    int slot = -1;
    for (int i = 0; i < MAX_VOLUMES; i++) {
        if (g_volumes[i].port == ahci_port) { slot = i; break; }
    }
    if (slot == -1) {
        for (int i = 0; i < MAX_VOLUMES; i++) {
            if (g_volumes[i].port == NULL) { slot = i; break; }
        }
    }
    if (slot == -1) slot = 0;

    FAT32_VOLUME *vol = &g_volumes[slot];
    vol->port = ahci_port;
    vol->lba_offset = 0;
    vol->initialized = 0;
    vol->cached_free_bytes = 0;
    vol->last_free_cluster_hint = 2;

    if (!read_sata_sector(vol->port, 0, 0, 1, sector_buf)) return 0;

    if ((sector_buf[0] != 0xEB && sector_buf[0] != 0xE9) && sector_buf[510] == 0x55 && sector_buf[511] == 0xAA) {
        UINT8 *part1 = &sector_buf[446];
        UINT32 part_lba = read_u32_le(&part1[8]);
        if (part_lba > 0 && part_lba < 100000000) {
            vol->lba_offset = part_lba;
            if (!read_sata_sector(vol->port, vol->lba_offset, 0, 1, sector_buf)) return 0;
        }
    }

    __builtin_memcpy(&vol->bpb, sector_buf, sizeof(FAT32_BPB));

    if (vol->bpb.bytes_per_sector != 512 || vol->bpb.sectors_per_cluster == 0) return 0;
    if (vol->bpb.reserved_sector_count == 0 || vol->bpb.num_fats == 0 || vol->bpb.table_size_32 == 0) return 0;

    UINT32 total_sectors = (vol->bpb.total_sectors_32 != 0) ? vol->bpb.total_sectors_32 : (UINT32)vol->bpb.total_sectors_16;
    if (total_sectors == 0) return 0;

    UINT64 fat_sectors = (UINT64)vol->bpb.num_fats * vol->bpb.table_size_32;
    UINT64 data_start = (UINT64)vol->bpb.reserved_sector_count + fat_sectors;
    if (data_start >= total_sectors) return 0;

    vol->total_clusters = (total_sectors - (UINT32)data_start) / vol->bpb.sectors_per_cluster;
    if (vol->total_clusters == 0) return 0;

    vol->first_data_sector = vol->lba_offset + (UINT32)data_start;
    vol->bytes_per_cluster = (UINT32)vol->bpb.sectors_per_cluster * 512U;
    vol->initialized = 1;

    return 1;
}

static UINT32 get_next_cluster(FAT32_VOLUME *vol, UINT32 cluster) {
    UINT8 fat_buf[512] __attribute__((aligned(16)));
    if (!vol || !vol->initialized || !valid_cluster(vol, cluster)) return FAT_EOC;

    UINT32 fat_sector = fat_sector_for_cluster(vol, cluster);
    UINT32 ent_offset = fat_offset_in_sector(vol, cluster);
    if (!read_sata_sector(vol->port, fat_sector, 0, 1, fat_buf)) return FAT_BAD;
    return read_u32_le(&fat_buf[ent_offset]) & 0x0FFFFFFFU;
}

static int set_next_cluster(FAT32_VOLUME *vol, UINT32 cluster, UINT32 value) {
    UINT8 fat_buf[512] __attribute__((aligned(16)));
    if (!vol || !vol->initialized || !valid_cluster(vol, cluster)) return 0;

    UINT32 fat_sector = fat_sector_for_cluster(vol, cluster);
    UINT32 ent_offset = fat_offset_in_sector(vol, cluster);

    for (UINT32 i = 0; i < vol->bpb.num_fats; i++) {
        UINT32 target = fat_sector + i * vol->bpb.table_size_32;
        if (!read_sata_sector(vol->port, target, 0, 1, fat_buf)) return 0;
        UINT32 old_val = read_u32_le(&fat_buf[ent_offset]);
        write_u32_le(&fat_buf[ent_offset], (old_val & 0xF0000000U) | (value & 0x0FFFFFFFU));
        if (!write_sata_sector(vol->port, target, 0, 1, fat_buf)) return 0;
    }
    vol->cached_free_bytes = 0;
    return 1;
}

static UINT32 find_free_cluster(FAT32_VOLUME *vol) {
    UINT8 fat_buf[512] __attribute__((aligned(16)));
    UINT32 fat_sector = 0xFFFFFFFFU;
    if (!vol || !vol->initialized) return 0;

    UINT32 start_c = vol->last_free_cluster_hint;
    if (!valid_cluster(vol, start_c)) start_c = 2;

    for (UINT32 c = start_c; c < vol->total_clusters + 2U; c++) {
        UINT32 cur_sec = fat_sector_for_cluster(vol, c);
        UINT32 ent_off = fat_offset_in_sector(vol, c);

        if (cur_sec != fat_sector) {
            fat_sector = cur_sec;
            if (!read_sata_sector(vol->port, fat_sector, 0, 1, fat_buf)) return 0;
        }

        if ((read_u32_le(&fat_buf[ent_off]) & 0x0FFFFFFFU) == FAT_FREE) {
            vol->last_free_cluster_hint = c + 1;
            return c;
        }
    }

    fat_sector = 0xFFFFFFFFU;
    for (UINT32 c = 2; c < start_c; c++) {
        UINT32 cur_sec = fat_sector_for_cluster(vol, c);
        UINT32 ent_off = fat_offset_in_sector(vol, c);

        if (cur_sec != fat_sector) {
            fat_sector = cur_sec;
            if (!read_sata_sector(vol->port, fat_sector, 0, 1, fat_buf)) return 0;
        }

        if ((read_u32_le(&fat_buf[ent_off]) & 0x0FFFFFFFU) == FAT_FREE) {
            vol->last_free_cluster_hint = c + 1;
            return c;
        }
    }
    return 0;
}

static void to_fat_name(const char *filename, char *fat_name) {
    for (int i = 0; i < 11; i++) fat_name[i] = ' ';
    if (!filename) return;

    int i = 0, j = 0, in_ext = 0;
    while (filename[i] && j < 11) {
        char ch = filename[i++];
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch == '.') { in_ext = 1; j = 8; continue; }

        if (!in_ext && j < 8) fat_name[j++] = ch;
        else if (in_ext && j >= 8 && j < 11) fat_name[j++] = ch;
    }
}

static void fat_name_to_string(const UINT8 *fat_name, char *out, int is_dir) {
    int pos = 0;
    for (int i = 0; i < 8; i++) {
        if (fat_name[i] != ' ') out[pos++] = (char)fat_name[i];
    }
    if (!is_dir && fat_name[8] != ' ') {
        out[pos++] = '.';
        for (int i = 8; i < 11; i++) {
            if (fat_name[i] != ' ') out[pos++] = (char)fat_name[i];
        }
    }
    out[pos] = '\0';
}

static inline int fat_names_match(const UINT8 *fat_name1, const char *fat_name2) {
    for (int i = 0; i < 11; i++) {
        if (fat_name1[i] != (UINT8)fat_name2[i]) return 0;
    }
    return 1;
}

static int is_regular_entry(const FAT32_DIR_ENTRY *entry) {
    if (entry->name[0] == 0x00 || entry->name[0] == 0xE5) return 0;
    if (entry->attr == 0x0F || (entry->attr & 0x08)) return 0;
    return 1;
}

static void split_path(const char *full_path, char *out_dir, char *out_filename) {
    out_dir[0] = '/'; out_dir[1] = '\0';
    out_filename[0] = '\0';
    if (!full_path || !full_path[0]) return;

    const char *p = full_path;
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':') p += 2;

    char clean[256];
    int len = 0;
    while (p[len] && len < 254) {
        clean[len] = (p[len] == '\\') ? '/' : p[len];
        len++;
    }
    clean[len] = '\0';
    while (len > 1 && clean[len - 1] == '/') clean[--len] = '\0';

    const char *last_slash = 0;
    for (int i = 0; clean[i]; i++) {
        if (clean[i] == '/') last_slash = &clean[i];
    }

    if (last_slash) {
        int dlen = (int)(last_slash - clean);
        if (dlen == 0) {
            out_dir[0] = '/'; out_dir[1] = '\0';
        } else {
            if (dlen > 127) dlen = 127;
            for (int k = 0; k < dlen; k++) out_dir[k] = clean[k];
            out_dir[dlen] = '\0';
        }
        const char *src = last_slash + 1;
        int k = 0; while (src[k] && k < 31) { out_filename[k] = src[k]; k++; } out_filename[k] = '\0';
    } else {
        out_dir[0] = '/'; out_dir[1] = '\0';
        int k = 0; while (clean[k] && k < 31) { out_filename[k] = clean[k]; k++; } out_filename[k] = '\0';
    }
}

static UINT32 resolve_path_to_cluster(FAT32_VOLUME *vol, const char *path) {
    if (!path || path[0] == '\0' || !kstrcmp(path, "/") || !kstrcmp(path, "C:") || !kstrcmp(path, "C:/") || !kstrcmp(path, "D:") || !kstrcmp(path, "D:/")) {
        return vol->bpb.root_cluster;
    }

    UINT32 current_cluster = vol->bpb.root_cluster;
    const char *p = path;
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':') p += 2;
    if (*p == '/' || *p == '\\') p++;

    char part[32];
    while (*p) {
        int len = 0;
        while (*p && *p != '/' && *p != '\\' && len < 31) part[len++] = *p++;
        part[len] = '\0';
        while (*p == '/' || *p == '\\') p++;
        if (len == 0) continue;

        char target_fat[11];
        to_fat_name(part, target_fat);

        UINT8 sector_buf[512] __attribute__((aligned(16)));
        UINT32 cluster = current_cluster;
        int found = 0;

        while (valid_cluster(vol, cluster) && !is_eoc(cluster)) {
            UINT32 lba = cluster_to_lba(vol, cluster);
            if (lba == 0) break;

            for (UINT32 s = 0; s < vol->bpb.sectors_per_cluster; s++) {
                if (!read_sata_sector(vol->port, lba + s, 0, 1, sector_buf)) return 0;
                FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;

                for (int e = 0; e < 16; e++) {
                    if (dir[e].name[0] == 0x00) break;
                    if (!is_regular_entry(&dir[e])) continue;

                    if (fat_names_match(dir[e].name, target_fat) && (dir[e].attr & 0x10)) {
                        current_cluster = ((UINT32)dir[e].first_cluster_high << 16) | dir[e].first_cluster_low;
                        if (current_cluster == 0) current_cluster = vol->bpb.root_cluster;
                        found = 1;
                        break;
                    }
                }
                if (found) break;
            }
            if (found) break;
            cluster = get_next_cluster(vol, cluster);
        }
        if (!found) return 0;
    }
    return current_cluster;
}

static int add_dir_entry(FAT32_VOLUME *vol, UINT32 parent_cluster, const char *fat_name, UINT8 attr, UINT32 first_cluster, UINT32 file_size) {
    UINT8 sector_buf[512] __attribute__((aligned(16)));
    UINT32 cluster = parent_cluster;
    UINT32 last_cluster = parent_cluster;

    while (valid_cluster(vol, cluster) && !is_eoc(cluster)) {
        last_cluster = cluster;
        UINT32 lba = cluster_to_lba(vol, cluster);
        for (UINT32 s = 0; s < vol->bpb.sectors_per_cluster; s++) {
            if (!read_sata_sector(vol->port, lba + s, 0, 1, sector_buf)) return 0;
            FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;

            for (int e = 0; e < 16; e++) {
                if (dir[e].name[0] == 0x00 || dir[e].name[0] == 0xE5) {
                    __builtin_memcpy(dir[e].name, fat_name, 11);
                    dir[e].attr = attr;
                    dir[e].nt_res = 0;
                    dir[e].file_size = file_size;
                    dir[e].wrt_date = 0x586C;
                    dir[e].wrt_time = 0x6000;
                    dir[e].first_cluster_high = (UINT16)(first_cluster >> 16);
                    dir[e].first_cluster_low = (UINT16)(first_cluster & 0xFFFF);
                    vol->cached_free_bytes = 0;
                    return write_sata_sector(vol->port, lba + s, 0, 1, sector_buf);
                }
            }
        }
        cluster = get_next_cluster(vol, cluster);
    }

    UINT32 new_dir_c = find_free_cluster(vol);
    if (!valid_cluster(vol, new_dir_c)) return 0;
    set_next_cluster(vol, new_dir_c, FAT_EOC);
    set_next_cluster(vol, last_cluster, new_dir_c);

    UINT32 new_lba = cluster_to_lba(vol, new_dir_c);
    __builtin_memset(sector_buf, 0, sizeof(sector_buf));
    FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;
    __builtin_memcpy(dir[0].name, fat_name, 11);
    dir[0].attr = attr;
    dir[0].nt_res = 0;
    dir[0].file_size = file_size;
    dir[0].wrt_date = 0x586C;
    dir[0].wrt_time = 0x6000;
    dir[0].first_cluster_high = (UINT16)(first_cluster >> 16);
    dir[0].first_cluster_low = (UINT16)(first_cluster & 0xFFFF);

    write_sata_sector(vol->port, new_lba, 0, 1, sector_buf);
    __builtin_memset(sector_buf, 0, sizeof(sector_buf));
    for (UINT32 s = 1; s < vol->bpb.sectors_per_cluster; s++) {
        write_sata_sector(vol->port, new_lba + s, 0, 1, sector_buf);
    }
    vol->cached_free_bytes = 0;
    return 1;
}

int fat32_mkdir(void *ahci_port, const char *path) {
    FAT32_VOLUME *vol = get_volume(ahci_port);
    if (!vol || !path || path[0] == '\0') return 0;

    char parent_path[128], dir_name[32];
    split_path(path, parent_path, dir_name);
    if (dir_name[0] == '\0') return 0;

    UINT32 parent_cluster = resolve_path_to_cluster(vol, parent_path);
    if (!valid_cluster(vol, parent_cluster)) parent_cluster = vol->bpb.root_cluster;

    char target_fat[11];
    to_fat_name(dir_name, target_fat);

    UINT8 sector_buf[512] __attribute__((aligned(16)));
    UINT32 cluster = parent_cluster;
    while (valid_cluster(vol, cluster) && !is_eoc(cluster)) {
        UINT32 lba = cluster_to_lba(vol, cluster);
        for (UINT32 s = 0; s < vol->bpb.sectors_per_cluster; s++) {
            if (!read_sata_sector(vol->port, lba + s, 0, 1, sector_buf)) return 0;
            FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;
            for (int e = 0; e < 16; e++) {
                if (dir[e].name[0] == 0x00) break;
                if (!is_regular_entry(&dir[e])) continue;
                if (fat_names_match(dir[e].name, target_fat) && (dir[e].attr & 0x10)) return 1;
            }
        }
        cluster = get_next_cluster(vol, cluster);
    }

    UINT32 new_cluster = find_free_cluster(vol);
    if (!valid_cluster(vol, new_cluster)) return 0;
    if (!set_next_cluster(vol, new_cluster, FAT_EOC)) return 0;

    UINT8 new_dir_sec[512] __attribute__((aligned(16)));
    __builtin_memset(new_dir_sec, 0, sizeof(new_dir_sec));
    FAT32_DIR_ENTRY *dots = (FAT32_DIR_ENTRY *)new_dir_sec;

    __builtin_memcpy(dots[0].name, ".          ", 11);
    dots[0].attr = 0x10;
    dots[0].first_cluster_high = (UINT16)(new_cluster >> 16);
    dots[0].first_cluster_low = (UINT16)(new_cluster & 0xFFFF);

    __builtin_memcpy(dots[1].name, "..         ", 11);
    dots[1].attr = 0x10;
    UINT32 p_val = (parent_cluster == vol->bpb.root_cluster) ? 0 : parent_cluster;
    dots[1].first_cluster_high = (UINT16)(p_val >> 16);
    dots[1].first_cluster_low = (UINT16)(p_val & 0xFFFF);

    UINT32 new_lba = cluster_to_lba(vol, new_cluster);
    write_sata_sector(vol->port, new_lba, 0, 1, new_dir_sec);

    __builtin_memset(new_dir_sec, 0, sizeof(new_dir_sec));
    for (UINT32 s = 1; s < vol->bpb.sectors_per_cluster; s++) {
        write_sata_sector(vol->port, new_lba + s, 0, 1, new_dir_sec);
    }

    return add_dir_entry(vol, parent_cluster, target_fat, 0x10, new_cluster, 0);
}

int fat32_write_file(void *ahci_port, const char *filename, void *buffer, UINT32 size) {
    FAT32_VOLUME *vol = get_volume(ahci_port);
    if (!vol || !filename) return 0;

    char dir_path[128], file_name[32];
    split_path(filename, dir_path, file_name);

    UINT32 parent_cluster = resolve_path_to_cluster(vol, dir_path);
    if (!valid_cluster(vol, parent_cluster)) parent_cluster = vol->bpb.root_cluster;

    char target_fat[11];
    to_fat_name(file_name, target_fat);

    UINT8 sector_buf[512] __attribute__((aligned(16)));
    UINT32 cluster = parent_cluster;
    int existing_found = 0;
    UINT32 exist_lba = 0;
    int exist_entry_idx = 0;
    FAT32_DIR_ENTRY exist_entry_copy;

    while (valid_cluster(vol, cluster) && !is_eoc(cluster)) {
        UINT32 lba = cluster_to_lba(vol, cluster);
        for (UINT32 s = 0; s < vol->bpb.sectors_per_cluster; s++) {
            if (!read_sata_sector(vol->port, lba + s, 0, 1, sector_buf)) break;
            FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;
            for (int e = 0; e < 16; e++) {
                if (dir[e].name[0] == 0x00) break;
                if (!is_regular_entry(&dir[e])) continue;
                if (fat_names_match(dir[e].name, target_fat) && !(dir[e].attr & 0x10)) {
                    existing_found = 1;
                    exist_lba = lba + s;
                    exist_entry_idx = e;
                    exist_entry_copy = dir[e];
                    break;
                }
            }
            if (existing_found) break;
        }
        if (existing_found) break;
        cluster = get_next_cluster(vol, cluster);
    }

    if (existing_found) {
        UINT32 old_c = ((UINT32)exist_entry_copy.first_cluster_high << 16) | exist_entry_copy.first_cluster_low;
        while (valid_cluster(vol, old_c) && !is_eoc(old_c)) {
            UINT32 nxt = get_next_cluster(vol, old_c);
            set_next_cluster(vol, old_c, FAT_FREE);
            if (nxt == FAT_BAD || nxt == FAT_FREE) break;
            old_c = nxt;
        }
    }

    UINT32 first_cluster = 0, prev_cluster = 0, bytes_written = 0;
    const UINT8 *src = (const UINT8*)buffer;

    while (bytes_written < size) {
        UINT32 new_cluster = find_free_cluster(vol);
        if (!valid_cluster(vol, new_cluster)) break;
        set_next_cluster(vol, new_cluster, FAT_EOC);

        if (first_cluster == 0) first_cluster = new_cluster;
        else if (prev_cluster != 0) set_next_cluster(vol, prev_cluster, new_cluster);
        prev_cluster = new_cluster;

        UINT32 lba = cluster_to_lba(vol, new_cluster);
        for (UINT32 s = 0; s < vol->bpb.sectors_per_cluster; s++) {
            UINT8 temp[512] __attribute__((aligned(16)));
            __builtin_memset(temp, 0, sizeof(temp));

            if (bytes_written < size) {
                UINT32 chunk = (size - bytes_written > 512) ? 512 : (size - bytes_written);
                __builtin_memcpy(temp, src + bytes_written, chunk);
                bytes_written += chunk;
            }
            write_sata_sector(vol->port, lba + s, 0, 1, temp);
        }
    }

    if (existing_found) {
        if (!read_sata_sector(vol->port, exist_lba, 0, 1, sector_buf)) return 0;
        FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;
        dir[exist_entry_idx].file_size = size;
        dir[exist_entry_idx].first_cluster_high = (UINT16)(first_cluster >> 16);
        dir[exist_entry_idx].first_cluster_low = (UINT16)(first_cluster & 0xFFFF);
        dir[exist_entry_idx].wrt_date = 0x586C;
        dir[exist_entry_idx].wrt_time = 0x6000;
        vol->cached_free_bytes = 0;
        return write_sata_sector(vol->port, exist_lba, 0, 1, sector_buf);
    } else {
        return add_dir_entry(vol, parent_cluster, target_fat, 0x20, first_cluster, size);
    }
}

int fat32_rename_file(void *ahci_port, const char *old_path, const char *new_name) {
    FAT32_VOLUME *vol = get_volume(ahci_port);
    if (!vol || !old_path || !new_name) return 0;

    char dir_path[128], old_name[32];
    split_path(old_path, dir_path, old_name);

    const char *last_slash = 0;
    for (int i = 0; new_name[i]; i++) {
        if (new_name[i] == '/' || new_name[i] == '\\') last_slash = &new_name[i];
    }
    const char *clean_new_name = last_slash ? last_slash + 1 : new_name;

    UINT32 parent_cluster = resolve_path_to_cluster(vol, dir_path);
    if (!valid_cluster(vol, parent_cluster)) parent_cluster = vol->bpb.root_cluster;

    UINT8 sector_buf[512] __attribute__((aligned(16)));
    char old_fat[11], new_fat[11];
    to_fat_name(old_name, old_fat);
    to_fat_name(clean_new_name, new_fat);

    UINT32 cluster = parent_cluster;
    while (valid_cluster(vol, cluster) && !is_eoc(cluster)) {
        UINT32 lba = cluster_to_lba(vol, cluster);
        for (UINT32 s = 0; s < vol->bpb.sectors_per_cluster; s++) {
            if (!read_sata_sector(vol->port, lba + s, 0, 1, sector_buf)) return 0;
            FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;

            for (int e = 0; e < 16; e++) {
                if (dir[e].name[0] == 0x00) return 0;
                if (!is_regular_entry(&dir[e])) continue;

                if (fat_names_match(dir[e].name, old_fat)) {
                    __builtin_memcpy(dir[e].name, new_fat, 11);
                    dir[e].nt_res = 0;
                    dir[e].wrt_date = 0x586C;
                    dir[e].wrt_time = 0x6000;
                    return write_sata_sector(vol->port, lba + s, 0, 1, sector_buf);
                }
            }
        }
        cluster = get_next_cluster(vol, cluster);
    }
    return 0;
}

int fat32_delete_file(void *ahci_port, const char *filename) {
    FAT32_VOLUME *vol = get_volume(ahci_port);
    if (!vol || !filename) return 0;

    char dir_path[128], file_name[32];
    split_path(filename, dir_path, file_name);

    UINT32 parent_cluster = resolve_path_to_cluster(vol, dir_path);
    if (!valid_cluster(vol, parent_cluster)) parent_cluster = vol->bpb.root_cluster;

    UINT8 sector_buf[512] __attribute__((aligned(16)));
    char target_fat[11];
    to_fat_name(file_name, target_fat);

    UINT32 cluster = parent_cluster;
    while (valid_cluster(vol, cluster) && !is_eoc(cluster)) {
        UINT32 lba = cluster_to_lba(vol, cluster);
        for (UINT32 s = 0; s < vol->bpb.sectors_per_cluster; s++) {
            if (!read_sata_sector(vol->port, lba + s, 0, 1, sector_buf)) return 0;
            FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;

            for (int e = 0; e < 16; e++) {
                if (dir[e].name[0] == 0x00) return 0;
                if (!is_regular_entry(&dir[e])) continue;

                if (fat_names_match(dir[e].name, target_fat)) {
                    UINT32 start_c = ((UINT32)dir[e].first_cluster_high << 16) | dir[e].first_cluster_low;
                    dir[e].name[0] = 0xE5;
                    write_sata_sector(vol->port, lba + s, 0, 1, sector_buf);

                    UINT32 cur_c = start_c;
                    while (valid_cluster(vol, cur_c) && !is_eoc(cur_c)) {
                        UINT32 next_c = get_next_cluster(vol, cur_c);
                        set_next_cluster(vol, cur_c, FAT_FREE);
                        if (next_c == FAT_BAD || next_c == FAT_FREE) break;
                        cur_c = next_c;
                    }
                    vol->cached_free_bytes = 0;
                    return 1;
                }
            }
        }
        cluster = get_next_cluster(vol, cluster);
    }
    return 0;
}

int fat32_read_file(void *ahci_port, const char *filename, void *buffer, UINT32 max_size) {
    FAT32_VOLUME *vol = get_volume(ahci_port);
    if (!vol || !filename || !buffer) return -1;

    char dir_path[128], file_name[32];
    split_path(filename, dir_path, file_name);

    UINT32 current_cluster = resolve_path_to_cluster(vol, dir_path);
    if (!valid_cluster(vol, current_cluster)) return -1;

    UINT8 sector_buf[512] __attribute__((aligned(16)));
    char target_fat[11];
    to_fat_name(file_name, target_fat);

    FAT32_DIR_ENTRY entry;
    int found = 0;

    while (valid_cluster(vol, current_cluster) && !is_eoc(current_cluster)) {
        UINT32 lba = cluster_to_lba(vol, current_cluster);
        for (UINT32 s = 0; s < vol->bpb.sectors_per_cluster; s++) {
            if (!read_sata_sector(vol->port, lba + s, 0, 1, sector_buf)) return -1;
            FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;

            for (int e = 0; e < 16; e++) {
                if (dir[e].name[0] == 0x00) return -1;
                if (!is_regular_entry(&dir[e])) continue;

                if (fat_names_match(dir[e].name, target_fat)) {
                    entry = dir[e];
                    found = 1;
                    break;
                }
            }
            if (found) break;
        }
        if (found) break;
        current_cluster = get_next_cluster(vol, current_cluster);
    }

    if (!found) return -1;

    UINT32 file_size = entry.file_size;
    UINT32 bytes_to_read = file_size < max_size ? file_size : max_size;
    UINT32 file_cluster = ((UINT32)entry.first_cluster_high << 16) | entry.first_cluster_low;
    if (file_cluster < 2) return 0;

    UINT8 *dest = (UINT8 *)buffer;
    UINT32 bytes_read = 0;

    while (bytes_read < bytes_to_read && valid_cluster(vol, file_cluster)) {
        UINT32 lba = cluster_to_lba(vol, file_cluster);
        for (UINT32 s = 0; s < vol->bpb.sectors_per_cluster; s++) {
            if (!read_sata_sector(vol->port, lba + s, 0, 1, sector_buf)) return -1;
            UINT32 rem = bytes_to_read - bytes_read;
            UINT32 chunk = rem > 512 ? 512 : rem;
            __builtin_memcpy(dest + bytes_read, sector_buf, chunk);
            bytes_read += chunk;
            if (bytes_read >= bytes_to_read) break;
        }
        if (bytes_read >= bytes_to_read) break;
        file_cluster = get_next_cluster(vol, file_cluster);
    }
    return (int)bytes_read;
}

int fat32_copy_file(void *ahci_port, const char *src_path, const char *dst_path) {
    if (!ahci_port || !src_path || !dst_path) return 0;
    if (!kstrcmp(src_path, dst_path)) return 1;

    int bytes = fat32_read_file(ahci_port, src_path, g_fat32_io_buf, sizeof(g_fat32_io_buf));
    if (bytes < 0) return 0;

    fat32_delete_file(ahci_port, dst_path);
    return fat32_write_file(ahci_port, dst_path, g_fat32_io_buf, (UINT32)bytes);
}

int fat32_move_file(void *ahci_port, const char *src_path, const char *dst_path) {
    if (!ahci_port || !src_path || !dst_path) return 0;
    if (!kstrcmp(src_path, dst_path)) return 1;

    char src_dir[128], src_file[32];
    char dst_dir[128], dst_file[32];
    split_path(src_path, src_dir, src_file);
    split_path(dst_path, dst_dir, dst_file);

    if (!kstrcmp(src_dir, dst_dir)) {
        return fat32_rename_file(ahci_port, src_path, dst_file);
    }

    int bytes = fat32_read_file(ahci_port, src_path, g_fat32_io_buf, sizeof(g_fat32_io_buf));
    if (bytes < 0) return 0;

    fat32_delete_file(ahci_port, dst_path);
    if (!fat32_write_file(ahci_port, dst_path, g_fat32_io_buf, (UINT32)bytes)) return 0;
    return fat32_delete_file(ahci_port, src_path);
}

int fat32_list_dir(void *ahci_port, const char *path, VeloDirEntry *out_entries, int max_entries) {
    FAT32_VOLUME *vol = get_volume(ahci_port);
    if (!vol || !out_entries || max_entries <= 0) return 0;

    UINT32 current_cluster = resolve_path_to_cluster(vol, path);
    if (!valid_cluster(vol, current_cluster)) return 0;

    UINT8 sector_buf[512] __attribute__((aligned(16)));
    int found_count = 0;
    UINT32 guard = 0;

    while (valid_cluster(vol, current_cluster) && !is_eoc(current_cluster) && found_count < max_entries) {
        if (++guard > 1024) break;

        UINT32 lba = cluster_to_lba(vol, current_cluster);
        if (lba == 0) break;

        for (UINT32 s = 0; s < vol->bpb.sectors_per_cluster && found_count < max_entries; s++) {
            if (!read_sata_sector(vol->port, lba + s, 0, 1, sector_buf)) return found_count;

            FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;
            for (UINT32 e = 0; e < 16 && found_count < max_entries; e++) {
                if (dir[e].name[0] == 0x00) return found_count;
                if (!is_regular_entry(&dir[e])) continue;
                if (dir[e].name[0] == '.') continue;

                VeloDirEntry *out = &out_entries[found_count];
                out->is_dir = (dir[e].attr & 0x10) ? 1 : 0;
                out->attr = dir[e].attr;
                out->size = dir[e].file_size;
                out->date = dir[e].wrt_date;
                out->time = dir[e].wrt_time;

                fat_name_to_string(dir[e].name, out->name, out->is_dir);
                found_count++;
            }
        }

        UINT32 next = get_next_cluster(vol, current_cluster);
        if (is_eoc(next) || !valid_cluster(vol, next)) break;
        current_cluster = next;
    }
    return found_count;
}

int fat32_list_root(void *ahci_port, char out_files[][32], int max_files) {
    VeloDirEntry entries[32];
    int count = fat32_list_dir(ahci_port, "/", entries, max_files);
    for (int i = 0; i < count; i++) {
        __builtin_memcpy(out_files[i], entries[i].name, 32);
    }
    return count;
}

// ====================================================
// 100% REIN DYNAMISCHE ERMITTLUNG DER FREIEN BYTES
// ====================================================
UINT64 fat32_get_free_bytes(void *ahci_port) {
    FAT32_VOLUME *vol = get_volume(ahci_port);
    if (!vol || !vol->initialized) {
        // Falls noch nicht formatiert, ist die gesamte physikalische Kapazitaet verfuegbar
        for (int i = 0; i < ahci_get_port_count(); i++) {
            AHCI_PORT_INFO *info = ahci_get_port_info(i);
            if (info && info->port_addr == ahci_port) {
                return (UINT64)info->sector_count * 512ULL;
            }
        }
        return 0;
    }
    if (vol->cached_free_bytes > 0) return vol->cached_free_bytes;

    // 1. Echten FSInfo-Sektor mit gueltigen Signaturen (0x41615252 / 0x61417272) pruefen
    UINT8 fsinfo_buf[512] __attribute__((aligned(16)));
    if (vol->bpb.fs_info > 0 && read_sata_sector(vol->port, vol->lba_offset + vol->bpb.fs_info, 0, 1, fsinfo_buf)) {
        UINT32 lead_sig = read_u32_le(&fsinfo_buf[0]);
        UINT32 struc_sig = read_u32_le(&fsinfo_buf[484]);
        if (lead_sig == 0x41615252 && struc_sig == 0x61417272) {
            UINT32 free_cl = read_u32_le(&fsinfo_buf[488]);
            if (free_cl > 0 && free_cl <= vol->total_clusters) {
                vol->cached_free_bytes = (UINT64)free_cl * (UINT64)vol->bytes_per_cluster;
                return vol->cached_free_bytes;
            }
        }
    }

    // 2. Dynamischer FAT-Scan ueber die Sektoren (kein Hardcoding)
    UINT8 fat_buf[512] __attribute__((aligned(16)));
    UINT32 free_count = 0;
    UINT32 fat_sec_start = vol->lba_offset + vol->bpb.reserved_sector_count;
    UINT32 fat_sectors_to_scan = vol->bpb.table_size_32;
    if (fat_sectors_to_scan > 512) fat_sectors_to_scan = 512;

    UINT32 total_cl_checked = 0;
    for (UINT32 s = 0; s < fat_sectors_to_scan && total_cl_checked < vol->total_clusters; s++) {
        if (!read_sata_sector(vol->port, fat_sec_start + s, 0, 1, fat_buf)) break;
        for (UINT32 e = 0; e < 128 && total_cl_checked < vol->total_clusters; e++) {
            if (s == 0 && e < 2) { total_cl_checked++; continue; }
            UINT32 val = read_u32_le(&fat_buf[e * 4]) & 0x0FFFFFFFU;
            if (val == FAT_FREE) free_count++;
            total_cl_checked++;
        }
    }

    if (total_cl_checked > 0) {
        UINT64 free_ratio = ((UINT64)free_count * (UINT64)vol->total_clusters) / total_cl_checked;
        vol->cached_free_bytes = free_ratio * (UINT64)vol->bytes_per_cluster;
    } else {
        vol->cached_free_bytes = (UINT64)(vol->total_clusters - 2) * (UINT64)vol->bytes_per_cluster;
    }
    return vol->cached_free_bytes;
}
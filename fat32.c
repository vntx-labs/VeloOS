#include "fat32.h"

static FAT32_BPB bpb;
static UINT32 first_data_sector = 0;
static UINT32 bytes_per_cluster = 0;
static UINT32 total_clusters = 0;
static void *g_port = 0;
static int g_initialized = 0;
static UINT64 g_cached_free_bytes = 0;
static UINT32 g_last_free_cluster_hint = 2;

static UINT8 g_fat32_io_buf[131072] __attribute__((aligned(4096)));

#define FAT_EOC           0x0FFFFFF8U
#define FAT_BAD           0x0FFFFFF7U
#define FAT_FREE          0x00000000U
#define FAT32_MAX_CLUSTER 0x0FFFFFF6U

static inline int kstrcmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return -1;
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static UINT32 get_total_sectors(void) {
    return bpb.total_sectors_32 != 0 ?
           bpb.total_sectors_32 : (UINT32)bpb.total_sectors_16;
}

static UINT32 cluster_to_lba(UINT32 cluster) {
    if (cluster < 2 || cluster >= total_clusters + 2) return 0;
    return first_data_sector + (cluster - 2U) * (UINT32)bpb.sectors_per_cluster;
}

static int valid_cluster(UINT32 cluster) {
    return cluster >= 2 && cluster < total_clusters + 2;
}

static int is_eoc(UINT32 cluster) {
    return cluster >= FAT_EOC;
}

static UINT32 fat_entry_offset(UINT32 cluster) {
    return cluster * 4U;
}

static UINT32 fat_sector_for_cluster(UINT32 cluster) {
    return bpb.reserved_sector_count +
           (fat_entry_offset(cluster) / bpb.bytes_per_sector);
}

static UINT32 fat_offset_in_sector(UINT32 cluster) {
    return fat_entry_offset(cluster) % bpb.bytes_per_sector;
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

static void invalidate_free_bytes_cache(void) {
    g_cached_free_bytes = 0;
}

int fat32_init(void *ahci_port) {
    UINT8 sector_buf[512] __attribute__((aligned(16)));

    g_port = ahci_port;
    g_initialized = 0;
    first_data_sector = 0;
    bytes_per_cluster = 0;
    total_clusters = 0;
    g_cached_free_bytes = 0;
    g_last_free_cluster_hint = 2;

    if (!g_port) return 0;

    if (!read_sata_sector(g_port, 0, 0, 1, sector_buf)) {
        return 0;
    }

    __builtin_memcpy(&bpb, sector_buf, sizeof(FAT32_BPB));

    if (bpb.bytes_per_sector != 512) return 0;
    if (bpb.sectors_per_cluster == 0) return 0;
    if ((bpb.sectors_per_cluster & (bpb.sectors_per_cluster - 1)) != 0) return 0;
    if (bpb.reserved_sector_count == 0) return 0;
    if (bpb.num_fats == 0) return 0;
    if (bpb.table_size_32 == 0) return 0;
    if (bpb.root_cluster < 2) return 0;

    UINT32 total_sectors = get_total_sectors();
    if (total_sectors == 0) return 0;

    UINT64 fat_sectors = (UINT64)bpb.num_fats * bpb.table_size_32;
    UINT64 data_start = (UINT64)bpb.reserved_sector_count + fat_sectors;

    if (data_start >= total_sectors) return 0;

    UINT32 data_sectors = total_sectors - (UINT32)data_start;
    total_clusters = data_sectors / bpb.sectors_per_cluster;

    if (total_clusters == 0) return 0;
    if (total_clusters > FAT32_MAX_CLUSTER - 1U) return 0;

    first_data_sector = (UINT32)data_start;
    bytes_per_cluster = (UINT32)bpb.sectors_per_cluster * 512U;

    if (!valid_cluster(bpb.root_cluster)) return 0;

    g_initialized = 1;
    return 1;
}

static UINT32 get_next_cluster(UINT32 cluster) {
    UINT8 fat_buf[512] __attribute__((aligned(16)));
    if (!g_initialized || !valid_cluster(cluster)) return FAT_EOC;

    UINT32 fat_sector = fat_sector_for_cluster(cluster);
    UINT32 ent_offset = fat_offset_in_sector(cluster);
    if (ent_offset > 508U) return FAT_BAD;

    if (!read_sata_sector(g_port, fat_sector, 0, 1, fat_buf)) return FAT_BAD;
    return read_u32_le(&fat_buf[ent_offset]) & 0x0FFFFFFFU;
}

static int set_next_cluster(UINT32 cluster, UINT32 value) {
    UINT8 fat_buf[512] __attribute__((aligned(16)));
    if (!g_initialized || !valid_cluster(cluster)) return 0;

    UINT32 fat_sector = fat_sector_for_cluster(cluster);
    UINT32 ent_offset = fat_offset_in_sector(cluster);
    if (ent_offset > 508U) return 0;

    for (UINT32 i = 0; i < bpb.num_fats; i++) {
        UINT32 target = fat_sector + i * bpb.table_size_32;
        if (!read_sata_sector(g_port, target, 0, 1, fat_buf)) return 0;
        UINT32 old_val = read_u32_le(&fat_buf[ent_offset]);
        write_u32_le(&fat_buf[ent_offset], (old_val & 0xF0000000U) | (value & 0x0FFFFFFFU));
        if (!write_sata_sector(g_port, target, 0, 1, fat_buf)) return 0;
    }
    invalidate_free_bytes_cache();
    return 1;
}

static UINT32 find_free_cluster(void) {
    UINT8 fat_buf[512] __attribute__((aligned(16)));
    UINT32 fat_sector = 0xFFFFFFFFU;
    if (!g_initialized) return 0;

    UINT32 start_c = g_last_free_cluster_hint;
    if (!valid_cluster(start_c)) start_c = 2;

    for (UINT32 c = start_c; c < total_clusters + 2U; c++) {
        UINT32 current_sector = fat_sector_for_cluster(c);
        UINT32 ent_offset = fat_offset_in_sector(c);
        if (ent_offset > 508U) return 0;

        if (current_sector != fat_sector) {
            fat_sector = current_sector;
            if (!read_sata_sector(g_port, fat_sector, 0, 1, fat_buf)) return 0;
        }

        if ((read_u32_le(&fat_buf[ent_offset]) & 0x0FFFFFFFU) == FAT_FREE) {
            g_last_free_cluster_hint = c + 1;
            return c;
        }
    }

    fat_sector = 0xFFFFFFFFU;
    for (UINT32 c = 2; c < start_c; c++) {
        UINT32 current_sector = fat_sector_for_cluster(c);
        UINT32 ent_offset = fat_offset_in_sector(c);
        if (ent_offset > 508U) return 0;

        if (current_sector != fat_sector) {
            fat_sector = current_sector;
            if (!read_sata_sector(g_port, fat_sector, 0, 1, fat_buf)) return 0;
        }

        if ((read_u32_le(&fat_buf[ent_offset]) & 0x0FFFFFFFU) == FAT_FREE) {
            g_last_free_cluster_hint = c + 1;
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
        if (ch == '.') { in_ext = 1; j = 8; continue; }

        if (!in_ext && j < 8) {
            fat_name[j++] = ch;
        } else if (in_ext && j >= 8 && j < 11) {
            fat_name[j++] = ch;
        }
    }
}

static void fat_name_to_string(const UINT8 *fat_name, char *out, int is_dir) {
    int pos = 0;
    for (int i = 0; i < 8; i++) {
        if (fat_name[i] != ' ') {
            out[pos++] = (char)fat_name[i];
        }
    }
    if (!is_dir && fat_name[8] != ' ') {
        out[pos++] = '.';
        for (int i = 8; i < 11; i++) {
            if (fat_name[i] != ' ') {
                out[pos++] = (char)fat_name[i];
            }
        }
    }
    out[pos] = '\0';
}

static inline int fat_names_match_exact(const UINT8 *fat_name1, const char *fat_name2) {
    for (int i = 0; i < 11; i++) {
        if (fat_name1[i] != (UINT8)fat_name2[i]) {
            return 0;
        }
    }
    return 1;
}

static int is_regular_entry(const FAT32_DIR_ENTRY *entry) {
    if (entry->name[0] == 0x00 || entry->name[0] == 0xE5) return 0;
    if (entry->attr == 0x0F) return 0;
    if (entry->attr & 0x08) return 0;
    return 1;
}

static void split_path(const char *full_path, char *out_dir, char *out_filename) {
    out_dir[0] = '/'; out_dir[1] = '\0';
    out_filename[0] = '\0';
    if (!full_path || !full_path[0]) return;

    const char *p = full_path;
    if ((p[0] == 'C' || p[0] == 'c') && p[1] == ':') p += 2;

    char clean[256];
    int len = 0;
    while (p[len] && len < 254) {
        clean[len] = (p[len] == '\\') ? '/' : p[len];
        len++;
    }
    clean[len] = '\0';

    while (len > 1 && clean[len - 1] == '/') {
        clean[--len] = '\0';
    }

    const char *last_slash = 0;
    for (int i = 0; clean[i]; i++) {
        if (clean[i] == '/') last_slash = &clean[i];
    }

    if (last_slash) {
        int dlen = (int)(last_slash - clean);
        if (dlen == 0) {
            out_dir[0] = '/'; 
            out_dir[1] = '\0';
        } else {
            if (dlen > 127) dlen = 127;
            for (int k = 0; k < dlen; k++) out_dir[k] = clean[k];
            out_dir[dlen] = '\0';
        }
        
        const char *src = last_slash + 1;
        int k = 0; 
        while (src[k] && k < 31) { 
            out_filename[k] = src[k]; 
            k++; 
        } 
        out_filename[k] = '\0';
    } else {
        out_dir[0] = '/'; out_dir[1] = '\0';
        int k = 0; 
        while (clean[k] && k < 31) { 
            out_filename[k] = clean[k]; 
            k++; 
        } 
        out_filename[k] = '\0';
    }
}

static UINT32 resolve_path_to_cluster(const char *path) {
    if (!path || path[0] == '\0' || !kstrcmp(path, "/") || !kstrcmp(path, "C:") || !kstrcmp(path, "C:/") || !kstrcmp(path, "c:") || !kstrcmp(path, "c:/")) {
        return bpb.root_cluster;
    }

    UINT32 current_cluster = bpb.root_cluster;
    const char *p = path;
    if ((p[0] == 'C' || p[0] == 'c') && p[1] == ':') p += 2;
    if (*p == '/' || *p == '\\') p++;

    char part[32];
    while (*p) {
        int len = 0;
        while (*p && *p != '/' && *p != '\\' && len < 31) {
            part[len++] = *p++;
        }
        part[len] = '\0';
        while (*p == '/' || *p == '\\') p++;
        if (len == 0) continue;

        char target_fat[11];
        to_fat_name(part, target_fat);

        UINT8 sector_buf[512] __attribute__((aligned(16)));
        UINT32 cluster = current_cluster;
        int found = 0;

        while (valid_cluster(cluster) && !is_eoc(cluster)) {
            UINT32 lba = cluster_to_lba(cluster);
            if (lba == 0) break;
            for (UINT32 s = 0; s < bpb.sectors_per_cluster; s++) {
                if (!read_sata_sector(g_port, lba + s, 0, 1, sector_buf)) return 0;
                FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;

                for (int e = 0; e < 16; e++) {
                    if (dir[e].name[0] == 0x00) break;
                    if (!is_regular_entry(&dir[e])) continue;

                    if (fat_names_match_exact(dir[e].name, target_fat) && (dir[e].attr & 0x10)) {
                        current_cluster = ((UINT32)dir[e].first_cluster_high << 16) | dir[e].first_cluster_low;
                        if (current_cluster == 0) current_cluster = bpb.root_cluster;
                        found = 1;
                        break;
                    }
                }
                if (found) break;
            }
            if (found) break;
            cluster = get_next_cluster(cluster);
        }

        if (!found) return 0;
    }

    return current_cluster;
}

static int add_dir_entry(void *ahci_port, UINT32 parent_cluster, const char *fat_name, UINT8 attr, UINT32 first_cluster, UINT32 file_size) {
    UINT8 sector_buf[512] __attribute__((aligned(16)));
    UINT32 cluster = parent_cluster;
    UINT32 last_cluster = parent_cluster;

    while (valid_cluster(cluster) && !is_eoc(cluster)) {
        last_cluster = cluster;
        UINT32 lba = cluster_to_lba(cluster);
        for (UINT32 s = 0; s < bpb.sectors_per_cluster; s++) {
            if (!read_sata_sector(g_port, lba + s, 0, 1, sector_buf)) return 0;
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
                    invalidate_free_bytes_cache();
                    return write_sata_sector(ahci_port, lba + s, 0, 1, sector_buf);
                }
            }
        }
        cluster = get_next_cluster(cluster);
    }

    UINT32 new_dir_cluster = find_free_cluster();
    if (!valid_cluster(new_dir_cluster)) return 0;
    set_next_cluster(new_dir_cluster, FAT_EOC);
    set_next_cluster(last_cluster, new_dir_cluster);

    UINT32 new_lba = cluster_to_lba(new_dir_cluster);
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

    write_sata_sector(ahci_port, new_lba, 0, 1, sector_buf);

    __builtin_memset(sector_buf, 0, sizeof(sector_buf));
    for (UINT32 s = 1; s < bpb.sectors_per_cluster; s++) {
        write_sata_sector(ahci_port, new_lba + s, 0, 1, sector_buf);
    }

    invalidate_free_bytes_cache();
    return 1;
}

int fat32_mkdir(void *ahci_port, const char *path) {
    if (!ahci_port || !path || path[0] == '\0') return 0;
    if (g_port != ahci_port || !g_initialized) {
        if (!fat32_init(ahci_port)) return 0;
    }

    char parent_path[128];
    char dir_name[32];
    split_path(path, parent_path, dir_name);
    if (dir_name[0] == '\0') return 0;

    UINT32 parent_cluster = resolve_path_to_cluster(parent_path);
    if (!valid_cluster(parent_cluster)) parent_cluster = bpb.root_cluster;

    char target_fat[11];
    to_fat_name(dir_name, target_fat);

    UINT8 sector_buf[512] __attribute__((aligned(16)));
    UINT32 cluster = parent_cluster;
    while (valid_cluster(cluster) && !is_eoc(cluster)) {
        UINT32 lba = cluster_to_lba(cluster);
        for (UINT32 s = 0; s < bpb.sectors_per_cluster; s++) {
            if (!read_sata_sector(g_port, lba + s, 0, 1, sector_buf)) return 0;
            FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;
            for (int e = 0; e < 16; e++) {
                if (dir[e].name[0] == 0x00) break;
                if (!is_regular_entry(&dir[e])) continue;
                if (fat_names_match_exact(dir[e].name, target_fat) && (dir[e].attr & 0x10)) {
                    return 1;
                }
            }
        }
        cluster = get_next_cluster(cluster);
    }

    UINT32 new_cluster = find_free_cluster();
    if (!valid_cluster(new_cluster)) return 0;
    if (!set_next_cluster(new_cluster, FAT_EOC)) return 0;

    UINT8 new_dir_sec[512] __attribute__((aligned(16)));
    __builtin_memset(new_dir_sec, 0, sizeof(new_dir_sec));
    FAT32_DIR_ENTRY *dots = (FAT32_DIR_ENTRY *)new_dir_sec;

    __builtin_memcpy(dots[0].name, ".          ", 11);
    dots[0].attr = 0x10;
    dots[0].first_cluster_high = (UINT16)(new_cluster >> 16);
    dots[0].first_cluster_low = (UINT16)(new_cluster & 0xFFFF);

    __builtin_memcpy(dots[1].name, "..         ", 11);
    dots[1].attr = 0x10;
    UINT32 p_val = (parent_cluster == bpb.root_cluster) ? 0 : parent_cluster;
    dots[1].first_cluster_high = (UINT16)(p_val >> 16);
    dots[1].first_cluster_low = (UINT16)(p_val & 0xFFFF);

    UINT32 new_lba = cluster_to_lba(new_cluster);
    write_sata_sector(ahci_port, new_lba, 0, 1, new_dir_sec);

    __builtin_memset(new_dir_sec, 0, sizeof(new_dir_sec));
    for (UINT32 s = 1; s < bpb.sectors_per_cluster; s++) {
        write_sata_sector(ahci_port, new_lba + s, 0, 1, new_dir_sec);
    }

    return add_dir_entry(ahci_port, parent_cluster, target_fat, 0x10, new_cluster, 0);
}

int fat32_write_file(void *ahci_port, const char *filename, void *buffer, UINT32 size) {
    if (!ahci_port || !filename) return 0;
    if (g_port != ahci_port || !g_initialized) {
        if (!fat32_init(ahci_port)) return 0;
    }

    char dir_path[128];
    char file_name[32];
    split_path(filename, dir_path, file_name);

    UINT32 parent_cluster = resolve_path_to_cluster(dir_path);
    if (!valid_cluster(parent_cluster)) parent_cluster = bpb.root_cluster;

    char target_fat[11];
    to_fat_name(file_name, target_fat);

    UINT8 sector_buf[512] __attribute__((aligned(16)));
    UINT32 cluster = parent_cluster;
    int existing_found = 0;
    UINT32 exist_lba = 0;
    int exist_entry_idx = 0;
    FAT32_DIR_ENTRY exist_entry_copy;

    while (valid_cluster(cluster) && !is_eoc(cluster)) {
        UINT32 lba = cluster_to_lba(cluster);
        for (UINT32 s = 0; s < bpb.sectors_per_cluster; s++) {
            if (!read_sata_sector(g_port, lba + s, 0, 1, sector_buf)) break;
            FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;
            for (int e = 0; e < 16; e++) {
                if (dir[e].name[0] == 0x00) break;
                if (!is_regular_entry(&dir[e])) continue;
                if (fat_names_match_exact(dir[e].name, target_fat) && !(dir[e].attr & 0x10)) {
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
        cluster = get_next_cluster(cluster);
    }

    if (existing_found) {
        UINT32 old_c = ((UINT32)exist_entry_copy.first_cluster_high << 16) | exist_entry_copy.first_cluster_low;
        while (valid_cluster(old_c) && !is_eoc(old_c)) {
            UINT32 nxt = get_next_cluster(old_c);
            set_next_cluster(old_c, FAT_FREE);
            if (nxt == FAT_BAD || nxt == FAT_FREE) break;
            old_c = nxt;
        }
    }

    UINT32 first_cluster = 0;
    UINT32 prev_cluster = 0;
    UINT32 bytes_written = 0;
    const UINT8 *src = (const UINT8*)buffer;

    while (bytes_written < size) {
        UINT32 new_cluster = find_free_cluster();
        if (!valid_cluster(new_cluster)) break;
        set_next_cluster(new_cluster, FAT_EOC);

        if (first_cluster == 0) {
            first_cluster = new_cluster;
        } else if (prev_cluster != 0) {
            set_next_cluster(prev_cluster, new_cluster);
        }
        prev_cluster = new_cluster;

        UINT32 lba = cluster_to_lba(new_cluster);
        for (UINT32 s = 0; s < bpb.sectors_per_cluster; s++) {
            UINT8 temp[512] __attribute__((aligned(16)));
            __builtin_memset(temp, 0, sizeof(temp));

            if (bytes_written < size) {
                UINT32 chunk = (size - bytes_written > 512) ? 512 : (size - bytes_written);
                __builtin_memcpy(temp, src + bytes_written, chunk);
                bytes_written += chunk;
            }
            write_sata_sector(ahci_port, lba + s, 0, 1, temp);
        }
    }

    if (existing_found) {
        if (!read_sata_sector(g_port, exist_lba, 0, 1, sector_buf)) return 0;
        FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;
        dir[exist_entry_idx].file_size = size;
        dir[exist_entry_idx].first_cluster_high = (UINT16)(first_cluster >> 16);
        dir[exist_entry_idx].first_cluster_low = (UINT16)(first_cluster & 0xFFFF);
        dir[exist_entry_idx].wrt_date = 0x586C;
        dir[exist_entry_idx].wrt_time = 0x6000;
        invalidate_free_bytes_cache();
        return write_sata_sector(ahci_port, exist_lba, 0, 1, sector_buf);
    } else {
        return add_dir_entry(ahci_port, parent_cluster, target_fat, 0x20, first_cluster, size);
    }
}

int fat32_rename_file(void *ahci_port, const char *old_path, const char *new_name) {
    if (!ahci_port || !old_path || !new_name) return 0;
    if (g_port != ahci_port || !g_initialized) {
        if (!fat32_init(ahci_port)) return 0;
    }

    char dir_path[128];
    char old_name[32];
    split_path(old_path, dir_path, old_name);

    const char *last_slash = 0;
    for (int i = 0; new_name[i]; i++) {
        if (new_name[i] == '/' || new_name[i] == '\\') last_slash = &new_name[i];
    }
    const char *clean_new_name = last_slash ? last_slash + 1 : new_name;

    UINT32 parent_cluster = resolve_path_to_cluster(dir_path);
    if (!valid_cluster(parent_cluster)) parent_cluster = bpb.root_cluster;

    UINT8 sector_buf[512] __attribute__((aligned(16)));
    char old_fat[11];
    to_fat_name(old_name, old_fat);

    char new_fat[11];
    to_fat_name(clean_new_name, new_fat);

    UINT32 cluster = parent_cluster;
    while (valid_cluster(cluster) && !is_eoc(cluster)) {
        UINT32 lba = cluster_to_lba(cluster);
        for (UINT32 s = 0; s < bpb.sectors_per_cluster; s++) {
            if (!read_sata_sector(g_port, lba + s, 0, 1, sector_buf)) return 0;
            FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;

            for (int e = 0; e < 16; e++) {
                if (dir[e].name[0] == 0x00) return 0;
                if (!is_regular_entry(&dir[e])) continue;

                if (fat_names_match_exact(dir[e].name, old_fat)) {
                    __builtin_memcpy(dir[e].name, new_fat, 11);
                    dir[e].nt_res = 0;
                    dir[e].wrt_date = 0x586C;
                    dir[e].wrt_time = 0x6000;
                    return write_sata_sector(ahci_port, lba + s, 0, 1, sector_buf);
                }
            }
        }
        cluster = get_next_cluster(cluster);
    }
    return 0;
}

int fat32_delete_file(void *ahci_port, const char *filename) {
    if (!ahci_port || !filename) return 0;
    if (g_port != ahci_port || !g_initialized) {
        if (!fat32_init(ahci_port)) return 0;
    }

    char dir_path[128];
    char file_name[32];
    split_path(filename, dir_path, file_name);

    UINT32 parent_cluster = resolve_path_to_cluster(dir_path);
    if (!valid_cluster(parent_cluster)) parent_cluster = bpb.root_cluster;

    UINT8 sector_buf[512] __attribute__((aligned(16)));
    char target_fat[11];
    to_fat_name(file_name, target_fat);

    UINT32 cluster = parent_cluster;
    while (valid_cluster(cluster) && !is_eoc(cluster)) {
        UINT32 lba = cluster_to_lba(cluster);
        for (UINT32 s = 0; s < bpb.sectors_per_cluster; s++) {
            if (!read_sata_sector(g_port, lba + s, 0, 1, sector_buf)) return 0;
            FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;

            for (int e = 0; e < 16; e++) {
                if (dir[e].name[0] == 0x00) return 0;
                if (!is_regular_entry(&dir[e])) continue;

                if (fat_names_match_exact(dir[e].name, target_fat)) {
                    UINT32 start_c = ((UINT32)dir[e].first_cluster_high << 16) | dir[e].first_cluster_low;
                    
                    dir[e].name[0] = 0xE5;
                    write_sata_sector(ahci_port, lba + s, 0, 1, sector_buf);

                    UINT32 cur_c = start_c;
                    while (valid_cluster(cur_c) && !is_eoc(cur_c)) {
                        UINT32 next_c = get_next_cluster(cur_c);
                        set_next_cluster(cur_c, FAT_FREE);
                        if (next_c == FAT_BAD || next_c == FAT_FREE) break;
                        cur_c = next_c;
                    }
                    invalidate_free_bytes_cache();
                    return 1;
                }
            }
        }
        cluster = get_next_cluster(cluster);
    }
    return 0;
}

int fat32_read_file(void *ahci_port, const char *filename, void *buffer, UINT32 max_size) {
    if (!ahci_port || !filename || !buffer) return -1;
    if (g_port != ahci_port || !g_initialized) {
        if (!fat32_init(ahci_port)) return -1;
    }

    char dir_path[128];
    char file_name[32];
    split_path(filename, dir_path, file_name);

    UINT32 current_cluster = resolve_path_to_cluster(dir_path);
    if (!valid_cluster(current_cluster)) return -1;

    UINT8 sector_buf[512] __attribute__((aligned(16)));
    char target_fat[11];
    to_fat_name(file_name, target_fat);

    FAT32_DIR_ENTRY entry;
    int found = 0;

    while (valid_cluster(current_cluster) && !is_eoc(current_cluster)) {
        UINT32 lba = cluster_to_lba(current_cluster);
        for (UINT32 s = 0; s < bpb.sectors_per_cluster; s++) {
            if (!read_sata_sector(g_port, lba + s, 0, 1, sector_buf)) return -1;
            FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;

            for (int e = 0; e < 16; e++) {
                if (dir[e].name[0] == 0x00) return -1;
                if (!is_regular_entry(&dir[e])) continue;

                if (fat_names_match_exact(dir[e].name, target_fat)) {
                    entry = dir[e];
                    found = 1;
                    break;
                }
            }
            if (found) break;
        }
        if (found) break;
        current_cluster = get_next_cluster(current_cluster);
    }

    if (!found) return -1;

    UINT32 file_size = entry.file_size;
    UINT32 bytes_to_read = file_size < max_size ? file_size : max_size;
    UINT32 file_cluster = ((UINT32)entry.first_cluster_high << 16) | entry.first_cluster_low;
    if (file_cluster < 2) return 0;

    UINT8 *dest = (UINT8 *)buffer;
    UINT32 bytes_read = 0;

    while (bytes_read < bytes_to_read && valid_cluster(file_cluster)) {
        UINT32 lba = cluster_to_lba(file_cluster);
        for (UINT32 s = 0; s < bpb.sectors_per_cluster; s++) {
            if (!read_sata_sector(ahci_port, lba + s, 0, 1, sector_buf)) return -1;
            UINT32 rem = bytes_to_read - bytes_read;
            UINT32 chunk = rem > 512 ? 512 : rem;
            __builtin_memcpy(dest + bytes_read, sector_buf, chunk);
            bytes_read += chunk;
            if (bytes_read >= bytes_to_read) break;
        }
        if (bytes_read >= bytes_to_read) break;
        file_cluster = get_next_cluster(file_cluster);
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
    if (!fat32_write_file(ahci_port, dst_path, g_fat32_io_buf, (UINT32)bytes)) {
        return 0;
    }
    return fat32_delete_file(ahci_port, src_path);
}

int fat32_list_dir(void *ahci_port, const char *path, VeloDirEntry *out_entries, int max_entries) {
    if (!ahci_port || !out_entries || max_entries <= 0) return 0;
    if (g_port != ahci_port || !g_initialized) {
        if (!fat32_init(ahci_port)) return 0;
    }

    UINT32 current_cluster = resolve_path_to_cluster(path);
    if (!valid_cluster(current_cluster)) return 0;

    UINT8 sector_buf[512] __attribute__((aligned(16)));
    int found_count = 0;
    UINT32 guard = 0;

    while (valid_cluster(current_cluster) && !is_eoc(current_cluster) && found_count < max_entries) {
        if (++guard > 1024) break;

        UINT32 lba = cluster_to_lba(current_cluster);
        if (lba == 0) break;

        for (UINT32 s = 0; s < bpb.sectors_per_cluster && found_count < max_entries; s++) {
            if (!read_sata_sector(g_port, lba + s, 0, 1, sector_buf)) return found_count;

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

        UINT32 next = get_next_cluster(current_cluster);
        if (is_eoc(next) || !valid_cluster(next)) break;
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

UINT64 fat32_get_free_bytes(void *ahci_port) {
    if (!ahci_port) return 0;
    if (g_port != ahci_port || !g_initialized) {
        if (!fat32_init(ahci_port)) return 0;
    }
    if (g_cached_free_bytes > 0) return g_cached_free_bytes;

    // Blitzschnelles Berechnen über FSInfo-Sektor
    UINT8 fsinfo_buf[512] __attribute__((aligned(16)));
    if (read_sata_sector(g_port, 1, 0, 1, fsinfo_buf)) {
        UINT32 free_cl = read_u32_le(&fsinfo_buf[488]);
        if (free_cl != 0xFFFFFFFF && free_cl <= total_clusters) {
            g_cached_free_bytes = (UINT64)free_cl * bytes_per_cluster;
            return g_cached_free_bytes;
        }
    }

    g_cached_free_bytes = (UINT64)(total_clusters - 20) * bytes_per_cluster;
    return g_cached_free_bytes;
}
#include "fat32.h"

static FAT32_BPB bpb;
static UINT32 first_data_sector = 0;
static UINT32 bytes_per_cluster = 0;
static UINT32 total_clusters = 0;
static void *g_port = 0;
static int g_initialized = 0;

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

int fat32_init(void *ahci_port) {
    UINT8 sector_buf[512] __attribute__((aligned(16)));

    g_port = ahci_port;
    g_initialized = 0;
    first_data_sector = 0;
    bytes_per_cluster = 0;
    total_clusters = 0;

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
    return 1;
}

static UINT32 find_free_cluster(void) {
    UINT8 fat_buf[512] __attribute__((aligned(16)));
    UINT32 fat_sector = 0xFFFFFFFFU;
    if (!g_initialized) return 0;

    for (UINT32 c = 2; c < total_clusters + 2U; c++) {
        UINT32 current_sector = fat_sector_for_cluster(c);
        UINT32 ent_offset = fat_offset_in_sector(c);
        if (ent_offset > 508U) return 0;

        if (current_sector != fat_sector) {
            fat_sector = current_sector;
            if (!read_sata_sector(g_port, fat_sector, 0, 1, fat_buf)) return 0;
        }

        if ((read_u32_le(&fat_buf[ent_offset]) & 0x0FFFFFFFU) == FAT_FREE) {
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
        if (ch >= 'a' && ch <= 'z') ch -= ('a' - 'A');
        if (!in_ext && j < 8) fat_name[j++] = ch;
        else if (in_ext && j >= 8 && j < 11) fat_name[j++] = ch;
    }
}

static void fat_name_to_string(const UINT8 *fat_name, char *out, int is_dir) {
    int pos = 0;
    for (int i = 0; i < 8; i++) {
        if (fat_name[i] != ' ') out[pos++] = fat_name[i];
    }
    if (!is_dir && fat_name[8] != ' ') {
        out[pos++] = '.';
        for (int i = 8; i < 11; i++) {
            if (fat_name[i] != ' ') out[pos++] = fat_name[i];
        }
    }
    out[pos] = '\0';
}

static int is_regular_entry(const FAT32_DIR_ENTRY *entry) {
    if (entry->name[0] == 0x00 || entry->name[0] == 0xE5) return 0;
    if (entry->attr == 0x0F) return 0;
    if (entry->attr & 0x08) return 0;
    return 1;
}

static UINT32 resolve_path_to_cluster(const char *path) {
    if (!path || path[0] == '\0' || !kstrcmp(path, "/") || !kstrcmp(path, "C:") || !kstrcmp(path, "C:/")) {
        return bpb.root_cluster;
    }

    UINT32 current_cluster = bpb.root_cluster;
    const char *p = path;
    if (p[0] == 'C' && p[1] == ':') p += 2;
    if (*p == '/') p++;

    char part[32];
    while (*p) {
        int len = 0;
        while (*p && *p != '/' && len < 31) part[len++] = *p++;
        part[len] = '\0';
        if (*p == '/') p++;
        if (len == 0) continue;

        char target_fat[11];
        to_fat_name(part, target_fat);

        UINT8 sector_buf[512] __attribute__((aligned(16)));
        UINT32 cluster = current_cluster;
        int found = 0;

        while (valid_cluster(cluster) && !is_eoc(cluster)) {
            UINT32 lba = cluster_to_lba(cluster);
            for (UINT32 s = 0; s < bpb.sectors_per_cluster; s++) {
                if (!read_sata_sector(g_port, lba + s, 0, 1, sector_buf)) return 0;
                FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;

                for (int e = 0; e < 16; e++) {
                    if (dir[e].name[0] == 0x00) break;
                    if (!is_regular_entry(&dir[e])) continue;

                    int match = 1;
                    for (int i = 0; i < 11; i++) {
                        if (dir[e].name[i] != (UINT8)target_fat[i]) { match = 0; break; }
                    }

                    if (match && (dir[e].attr & 0x10)) {
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

int fat32_mkdir(void *ahci_port, const char *path) {
    if (!ahci_port || !path || path[0] == '\0') return 0;
    if (g_port != ahci_port || !g_initialized) {
        if (!fat32_init(ahci_port)) return 0;
    }

    char parent_path[128];
    char dir_name[32];
    int len = 0; while (path[len] && len < 127) { parent_path[len] = path[len]; len++; } parent_path[len] = '\0';

    char *last_slash = 0;
    for (int i = 0; parent_path[i]; i++) {
        if (parent_path[i] == '/') last_slash = &parent_path[i];
    }

    if (!last_slash || last_slash == parent_path) {
        parent_path[0] = '/'; parent_path[1] = '\0';
        const char *src = (path[0] == '/') ? path + 1 : path;
        int k = 0; while (src[k] && k < 31) { dir_name[k] = src[k]; k++; } dir_name[k] = '\0';
    } else {
        *last_slash = '\0';
        const char *src = last_slash + 1;
        int k = 0; while (src[k] && k < 31) { dir_name[k] = src[k]; k++; } dir_name[k] = '\0';
    }

    UINT32 parent_cluster = resolve_path_to_cluster(parent_path);
    if (!valid_cluster(parent_cluster)) parent_cluster = bpb.root_cluster;

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

    UINT8 sector_buf[512] __attribute__((aligned(16)));
    UINT32 cluster = parent_cluster;
    char target_fat[11];
    to_fat_name(dir_name, target_fat);

    while (valid_cluster(cluster) && !is_eoc(cluster)) {
        UINT32 lba = cluster_to_lba(cluster);
        for (UINT32 s = 0; s < bpb.sectors_per_cluster; s++) {
            if (!read_sata_sector(g_port, lba + s, 0, 1, sector_buf)) return 0;
            FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;

            for (int e = 0; e < 16; e++) {
                if (dir[e].name[0] == 0x00 || dir[e].name[0] == 0xE5) {
                    __builtin_memcpy(dir[e].name, target_fat, 11);
                    dir[e].attr = 0x10;
                    dir[e].file_size = 0;
                    dir[e].wrt_date = 0x586C;
                    dir[e].wrt_time = 0x6000;
                    dir[e].first_cluster_high = (UINT16)(new_cluster >> 16);
                    dir[e].first_cluster_low = (UINT16)(new_cluster & 0xFFFF);
                    return write_sata_sector(ahci_port, lba + s, 0, 1, sector_buf);
                }
            }
        }
        cluster = get_next_cluster(cluster);
    }

    return 0;
}

int fat32_write_file(void *ahci_port, const char *filename, void *buffer, UINT32 size) {
    if (!ahci_port || !filename) return 0;
    if (g_port != ahci_port || !g_initialized) {
        if (!fat32_init(ahci_port)) return 0;
    }

    UINT32 new_cluster = 0;
    if (size > 0) {
        new_cluster = find_free_cluster();
        if (!valid_cluster(new_cluster)) return 0;
        if (!set_next_cluster(new_cluster, FAT_EOC)) return 0;

        UINT8 temp[512] __attribute__((aligned(16)));
        __builtin_memset(temp, 0, sizeof(temp));
        UINT32 to_copy = size > 512 ? 512 : size;
        __builtin_memcpy(temp, buffer, to_copy);
        write_sata_sector(ahci_port, cluster_to_lba(new_cluster), 0, 1, temp);
    }

    UINT8 sector_buf[512] __attribute__((aligned(16)));
    char target_fat[11];
    to_fat_name(filename, target_fat);

    UINT32 lba = cluster_to_lba(bpb.root_cluster);
    if (!read_sata_sector(g_port, lba, 0, 1, sector_buf)) return 0;
    FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;

    for (int e = 0; e < 16; e++) {
        if (dir[e].name[0] == 0x00 || dir[e].name[0] == 0xE5) {
            __builtin_memcpy(dir[e].name, target_fat, 11);
            dir[e].attr = 0x20;
            dir[e].file_size = size;
            dir[e].wrt_date = 0x586C;
            dir[e].wrt_time = 0x6000;
            dir[e].first_cluster_high = (UINT16)(new_cluster >> 16);
            dir[e].first_cluster_low = (UINT16)(new_cluster & 0xFFFF);
            return write_sata_sector(ahci_port, lba, 0, 1, sector_buf);
        }
    }
    return 0;
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
        if (++guard > total_clusters) break;

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

int fat32_read_file(void *ahci_port, const char *filename, void *buffer, UINT32 max_size) {
    if (!ahci_port || !filename || !buffer) return -1;
    if (g_port != ahci_port || !g_initialized) {
        if (!fat32_init(ahci_port)) return -1;
    }

    char dir_path[128];
    char file_name[32];
    dir_path[0] = '/'; dir_path[1] = '\0';
    
    const char *last_slash = 0;
    for (int i = 0; filename[i]; i++) {
        if (filename[i] == '/') last_slash = &filename[i];
    }

    if (last_slash) {
        int dlen = (int)(last_slash - filename);
        if (dlen == 0) {
            dir_path[0] = '/'; dir_path[1] = '\0';
        } else {
            if (dlen > 127) dlen = 127;
            for (int k = 0; k < dlen; k++) dir_path[k] = filename[k];
            dir_path[dlen] = '\0';
        }
        const char *src = last_slash + 1;
        int k = 0; while (src[k] && k < 31) { file_name[k] = src[k]; k++; } file_name[k] = '\0';
    } else {
        int k = 0; while (filename[k] && k < 31) { file_name[k] = filename[k]; k++; } file_name[k] = '\0';
    }

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

                int match = 1;
                for (int i = 0; i < 11; i++) {
                    if (dir[e].name[i] != (UINT8)target_fat[i]) { match = 0; break; }
                }

                if (match) {
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
    UINT8 fat_buf[512] __attribute__((aligned(16)));
    UINT32 free_count = 0;
    UINT32 checked = 0;
    UINT32 limit = total_clusters > 8192 ? 8192 : total_clusters;

    for (UINT32 s = 0; s < (limit * 4 + 511) / 512; s++) {
        if (!read_sata_sector(g_port, bpb.reserved_sector_count + s, 0, 1, fat_buf)) break;
        for (int e = 0; e < 128 && checked < total_clusters; e++, checked++) {
            if ((read_u32_le(&fat_buf[e * 4]) & 0x0FFFFFFFU) == FAT_FREE) {
                free_count++;
            }
        }
    }
    return (UINT64)free_count * bytes_per_cluster;
}
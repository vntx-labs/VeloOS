#include "fat32.h"

/*
 * Sicherer FAT32-Treiber mit integrierter Formatier- & Verzeichnis-Lese-Funktion
 */

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
    UINT8 sector_buf[512];

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

int fat32_format(void *ahci_port, UINT64 disk_sectors) {
    if (!ahci_port) return 0;
    
    if (disk_sectors == 0) disk_sectors = 131072;

    UINT32 total_sec = (disk_sectors > 0xFFFFFFFFULL) ? 0xFFFFFFFFU : (UINT32)disk_sectors;
    UINT8 spc = 8;
    UINT16 reserved = 32;
    UINT8 num_fats = 2;

    UINT32 fat_size = ((total_sec / spc) * 4U + 511U) / 512U;

    UINT8 sector[512];
    __builtin_memset(sector, 0, 512);

    sector[0] = 0xEB; sector[1] = 0x58; sector[2] = 0x90;
    __builtin_memcpy(&sector[3], "MSDOS5.0", 8);
    sector[11] = 0x00; sector[12] = 0x02;
    sector[13] = spc;
    sector[14] = (UINT8)(reserved & 0xFF);
    sector[15] = (UINT8)((reserved >> 8) & 0xFF);
    sector[16] = num_fats;
    sector[17] = 0; sector[18] = 0;
    sector[19] = 0; sector[20] = 0;
    sector[21] = 0xF8;
    sector[22] = 0; sector[23] = 0;
    sector[24] = 0x3F; sector[25] = 0x00;
    sector[26] = 0xFF; sector[27] = 0x00;
    write_u32_le(&sector[28], 0);
    write_u32_le(&sector[32], total_sec);
    write_u32_le(&sector[36], fat_size);
    sector[40] = 0; sector[41] = 0;
    sector[42] = 0; sector[43] = 0;
    write_u32_le(&sector[44], 2);
    sector[48] = 1; sector[49] = 0;
    sector[50] = 6; sector[51] = 0;
    sector[64] = 0x80;
    sector[66] = 0x29;
    write_u32_le(&sector[67], 0x12345678);
    __builtin_memcpy(&sector[71], "VELOOS DISK", 11);
    __builtin_memcpy(&sector[82], "FAT32   ", 8);
    sector[510] = 0x55; sector[511] = 0xAA;

    if (!write_sata_sector(ahci_port, 0, 0, 1, sector)) return 0;
    if (!write_sata_sector(ahci_port, 6, 0, 1, sector)) return 0;

    __builtin_memset(sector, 0, 512);
    write_u32_le(&sector[0], 0x41615252);
    write_u32_le(&sector[484], 0x61417272);
    write_u32_le(&sector[488], 0xFFFFFFFF);
    write_u32_le(&sector[492], 2);
    sector[510] = 0x55; sector[511] = 0xAA;
    if (!write_sata_sector(ahci_port, 1, 0, 1, sector)) return 0;

    __builtin_memset(sector, 0, 512);
    write_u32_le(&sector[0], 0x0FFFFFF8);
    write_u32_le(&sector[4], 0x0FFFFFFF);
    write_u32_le(&sector[8], 0x0FFFFFFF);

    if (!write_sata_sector(ahci_port, reserved, 0, 1, sector)) return 0;
    if (!write_sata_sector(ahci_port, reserved + fat_size, 0, 1, sector)) return 0;

    __builtin_memset(sector, 0, 512);
    for (UINT32 s = 1; s < fat_size; s++) {
        if (!write_sata_sector(ahci_port, reserved + s, 0, 1, sector)) return 0;
        if (!write_sata_sector(ahci_port, reserved + fat_size + s, 0, 1, sector)) return 0;
    }

    UINT32 root_lba = reserved + (num_fats * fat_size);
    for (UINT32 s = 0; s < spc; s++) {
        if (!write_sata_sector(ahci_port, root_lba + s, 0, 1, sector)) return 0;
    }

    return fat32_init(ahci_port);
}

static UINT32 get_next_cluster(UINT32 cluster) {
    UINT8 fat_buf[512];

    if (!g_initialized || !valid_cluster(cluster)) return FAT_EOC;

    UINT32 fat_sector = fat_sector_for_cluster(cluster);
    UINT32 ent_offset = fat_offset_in_sector(cluster);

    if (ent_offset > 508U) return FAT_BAD;

    if (!read_sata_sector(g_port, fat_sector, 0, 1, fat_buf)) {
        return FAT_BAD;
    }

    return read_u32_le(&fat_buf[ent_offset]) & 0x0FFFFFFFU;
}

static int set_next_cluster(UINT32 cluster, UINT32 value) {
    UINT8 fat_buf[512];

    if (!g_initialized || !valid_cluster(cluster)) return 0;
    if (value > FAT_EOC && value != FAT_BAD) return 0;

    UINT32 fat_sector = fat_sector_for_cluster(cluster);
    UINT32 ent_offset = fat_offset_in_sector(cluster);
    if (ent_offset > 508U) return 0;

    for (UINT32 i = 0; i < bpb.num_fats; i++) {
        UINT32 target_sector = fat_sector + i * bpb.table_size_32;

        if (!read_sata_sector(g_port, target_sector, 0, 1, fat_buf)) {
            return 0;
        }

        UINT32 old_value = read_u32_le(&fat_buf[ent_offset]);
        UINT32 new_value = (old_value & 0xF0000000U) | (value & 0x0FFFFFFFU);
        write_u32_le(&fat_buf[ent_offset], new_value);

        if (!write_sata_sector(g_port, target_sector, 0, 1, fat_buf)) {
            return 0;
        }
    }

    return 1;
}

static UINT32 find_free_cluster(void) {
    UINT8 fat_buf[512];
    UINT32 fat_sector = 0xFFFFFFFFU;

    if (!g_initialized) return 0;

    for (UINT32 c = 2; c < total_clusters + 2U; c++) {
        UINT32 current_sector = fat_sector_for_cluster(c);
        UINT32 ent_offset = fat_offset_in_sector(c);

        if (ent_offset > 508U) return 0;

        if (current_sector != fat_sector) {
            fat_sector = current_sector;
            if (!read_sata_sector(g_port, fat_sector, 0, 1, fat_buf)) {
                return 0;
            }
        }

        if ((read_u32_le(&fat_buf[ent_offset]) & 0x0FFFFFFFU) == FAT_FREE) {
            return c;
        }
    }

    return 0;
}

static int free_cluster_chain(UINT32 first_cluster) {
    UINT32 cluster = first_cluster;
    UINT32 guard = 0;

    while (valid_cluster(cluster) && !is_eoc(cluster)) {
        if (++guard > total_clusters) return 0;

        UINT32 next = get_next_cluster(cluster);
        if (next == FAT_BAD) return 0;

        if (!set_next_cluster(cluster, FAT_FREE)) return 0;

        if (is_eoc(next)) break;
        if (!valid_cluster(next)) return 0;

        cluster = next;
    }

    return 1;
}

static void to_fat_name(const char *filename, char *fat_name) {
    for (int i = 0; i < 11; i++) fat_name[i] = ' ';

    if (!filename) return;

    int i = 0;
    int j = 0;
    int in_extension = 0;

    while (filename[i] && j < 11) {
        char ch = filename[i++];

        if (ch == '.') {
            in_extension = 1;
            j = 8;
            continue;
        }

        if (ch >= 'a' && ch <= 'z') ch -= ('a' - 'A');

        if (!in_extension && j < 8) {
            fat_name[j++] = ch;
        } else if (in_extension && j >= 8 && j < 11) {
            fat_name[j++] = ch;
        }
    }
}

static int names_equal(const UINT8 *a, const char *b) {
    char fat_name[11];
    to_fat_name(b, fat_name);

    for (int i = 0; i < 11; i++) {
        if (a[i] != (UINT8)fat_name[i]) return 0;
    }

    return 1;
}

static int is_regular_short_entry(const FAT32_DIR_ENTRY *entry) {
    if (entry->name[0] == 0x00) return 0;
    if (entry->name[0] == 0xE5) return 0;
    if (entry->attr == 0x0F) return 0;
    if (entry->attr & 0x08) return 0;
    if (entry->attr & 0x10) return 0;
    return 1;
}

static int find_directory_entry(const char *filename,
                                UINT32 *out_lba,
                                UINT32 *out_index,
                                FAT32_DIR_ENTRY *out_entry) {
    UINT8 sector_buf[512];
    UINT32 current_cluster = bpb.root_cluster;
    UINT32 guard = 0;

    while (valid_cluster(current_cluster) && !is_eoc(current_cluster)) {
        if (++guard > total_clusters) return 0;

        UINT32 lba = cluster_to_lba(current_cluster);
        if (lba == 0) return 0;

        for (UINT32 s = 0; s < bpb.sectors_per_cluster; s++) {
            if (!read_sata_sector(g_port, lba + s, 0, 1, sector_buf)) return 0;

            FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;
            for (UINT32 entry = 0; entry < 16; entry++) {
                if (dir[entry].name[0] == 0x00) return 0;
                if (!is_regular_short_entry(&dir[entry])) continue;

                if (names_equal(dir[entry].name, filename)) {
                    if (out_lba) *out_lba = lba + s;
                    if (out_index) *out_index = entry;
                    if (out_entry) __builtin_memcpy(out_entry, &dir[entry], sizeof(FAT32_DIR_ENTRY));
                    return 1;
                }
            }
        }

        UINT32 next = get_next_cluster(current_cluster);
        if (is_eoc(next)) break;
        if (!valid_cluster(next)) return 0;
        current_cluster = next;
    }

    return 0;
}

static int find_free_directory_entry(UINT32 *out_lba,
                                     UINT32 *out_index,
                                     int *out_was_end_marker) {
    UINT8 sector_buf[512];
    UINT32 current_cluster = bpb.root_cluster;
    UINT32 previous_cluster = 0;
    UINT32 guard = 0;

    while (valid_cluster(current_cluster)) {
        if (++guard > total_clusters) return 0;

        UINT32 lba = cluster_to_lba(current_cluster);
        for (UINT32 s = 0; s < bpb.sectors_per_cluster; s++) {
            if (!read_sata_sector(g_port, lba + s, 0, 1, sector_buf)) return 0;

            FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;
            for (UINT32 entry = 0; entry < 16; entry++) {
                if (dir[entry].name[0] == 0x00) {
                    if (out_lba) *out_lba = lba + s;
                    if (out_index) *out_index = entry;
                    if (out_was_end_marker) *out_was_end_marker = 1;
                    return 1;
                }

                if (dir[entry].name[0] == 0xE5) {
                    if (out_lba) *out_lba = lba + s;
                    if (out_index) *out_index = entry;
                    if (out_was_end_marker) *out_was_end_marker = 0;
                    return 1;
                }
            }
        }

        previous_cluster = current_cluster;
        UINT32 next = get_next_cluster(current_cluster);

        if (is_eoc(next)) {
            UINT32 new_cluster = find_free_cluster();
            if (new_cluster == 0) return 0;

            if (!set_next_cluster(previous_cluster, new_cluster)) return 0;
            if (!set_next_cluster(new_cluster, FAT_EOC)) return 0;

            UINT8 zero_sector[512];
            __builtin_memset(zero_sector, 0, sizeof(zero_sector));
            UINT32 new_lba = cluster_to_lba(new_cluster);
            for (UINT32 s = 0; s < bpb.sectors_per_cluster; s++) {
                if (!write_sata_sector(g_port, new_lba + s, 0, 1, zero_sector)) {
                    return 0;
                }
            }

            if (out_lba) *out_lba = new_lba;
            if (out_index) *out_index = 0;
            if (out_was_end_marker) *out_was_end_marker = 1;
            return 1;
        }

        if (!valid_cluster(next)) return 0;
        current_cluster = next;
    }

    return 0;
}

static int allocate_cluster_chain(UINT32 count, UINT32 *out_first) {
    if (!out_first) return 0;
    *out_first = 0;

    if (count == 0) return 1;
    if (count > total_clusters) return 0;

    UINT32 first = 0;
    UINT32 previous = 0;

    for (UINT32 i = 0; i < count; i++) {
        UINT32 cluster = find_free_cluster();
        if (cluster == 0) {
            if (first != 0) free_cluster_chain(first);
            return 0;
        }

        if (!set_next_cluster(cluster, FAT_EOC)) {
            if (first != 0) free_cluster_chain(first);
            return 0;
        }

        if (previous != 0) {
            if (!set_next_cluster(previous, cluster)) {
                if (first != 0) free_cluster_chain(first);
                return 0;
            }
        } else {
            first = cluster;
        }

        previous = cluster;
    }

    *out_first = first;
    return 1;
}

static int write_file_clusters(UINT32 first_cluster,
                               const UINT8 *buffer,
                               UINT32 size) {
    UINT8 temp_sec[512];
    UINT32 cluster = first_cluster;
    UINT32 bytes_written = 0;
    UINT32 guard = 0;

    while (valid_cluster(cluster) && bytes_written < size) {
        if (++guard > total_clusters) return 0;

        UINT32 cluster_lba = cluster_to_lba(cluster);
        if (cluster_lba == 0) return 0;

        for (UINT32 s = 0; s < bpb.sectors_per_cluster; s++) {
            __builtin_memset(temp_sec, 0, sizeof(temp_sec));

            UINT32 remaining = size - bytes_written;
            UINT32 chunk = remaining > 512U ? 512U : remaining;

            if (chunk > 0) {
                __builtin_memcpy(temp_sec, buffer + bytes_written, chunk);
                bytes_written += chunk;
            }

            if (!write_sata_sector(g_port, cluster_lba + s, 0, 1, temp_sec)) {
                return 0;
            }

            if (bytes_written >= size) break;
        }

        if (bytes_written >= size) return 1;

        UINT32 next = get_next_cluster(cluster);
        if (is_eoc(next) || !valid_cluster(next)) return 0;
        cluster = next;
    }

    return bytes_written == size;
}

/*
 * Optimierter FAT32 Datei-Leser mit Direct-DMA Cluster-Übertragung
 */
int fat32_read_file(void *ahci_port,
                    const char *filename,
                    void *buffer,
                    UINT32 max_size) {
    if (!ahci_port || !filename || !buffer) return -1;
    if (g_port != ahci_port || !g_initialized) {
        if (!fat32_init(ahci_port)) return -1;
    }

    UINT32 entry_lba = 0;
    UINT32 entry_index = 0;
    FAT32_DIR_ENTRY entry;

    if (!find_directory_entry(filename, &entry_lba, &entry_index, &entry)) {
        return -1;
    }

    (void)entry_lba;
    (void)entry_index;

    UINT32 file_size = entry.file_size;
    if (max_size == 0 || file_size == 0) return 0;

    UINT32 bytes_to_read = file_size < max_size ? file_size : max_size;
    UINT32 file_cluster = ((UINT32)entry.first_cluster_high << 16) |
                          entry.first_cluster_low;

    if (file_cluster < 2) return -1;

    UINT8 *dest = (UINT8 *)buffer;
    UINT32 bytes_read = 0;
    UINT32 guard = 0;
    UINT32 cluster_bytes = (UINT32)bpb.sectors_per_cluster * 512U;
    UINT8 sector_buf[512];

    while (bytes_read < bytes_to_read && valid_cluster(file_cluster)) {
        if (++guard > total_clusters) return -1;

        UINT32 lba = cluster_to_lba(file_cluster);
        if (lba == 0) return -1;

        UINT32 remaining = bytes_to_read - bytes_read;

        if (remaining >= cluster_bytes) {
            /* Ganzen Cluster in einem einzigen AHCI-DMA-Transfer einlesen (Turbo-Speed) */
            if (!read_sata_sector(ahci_port, lba, 0, bpb.sectors_per_cluster, dest + bytes_read)) {
                return -1;
            }
            bytes_read += cluster_bytes;
        } else {
            /* Letzten Cluster sektorweise einlesen */
            for (UINT32 s = 0; s < bpb.sectors_per_cluster; s++) {
                if (!read_sata_sector(ahci_port, lba + s, 0, 1, sector_buf)) {
                    return -1;
                }

                UINT32 rem_sec = bytes_to_read - bytes_read;
                UINT32 chunk = rem_sec > 512U ? 512U : rem_sec;
                __builtin_memcpy(dest + bytes_read, sector_buf, chunk);
                bytes_read += chunk;

                if (bytes_read >= bytes_to_read) break;
            }
        }

        if (bytes_read >= bytes_to_read) break;

        UINT32 next = get_next_cluster(file_cluster);
        if (is_eoc(next) || !valid_cluster(next)) break;
        file_cluster = next;
    }

    return (int)bytes_read;
}

int fat32_write_file(void *ahci_port,
                     const char *filename,
                     void *buffer,
                     UINT32 size) {
    if (!ahci_port || !filename) return 0;
    if (size > 0 && !buffer) return 0;
    if (g_port != ahci_port || !g_initialized) {
        if (!fat32_init(ahci_port)) return 0;
    }

    UINT32 target_lba = 0;
    UINT32 target_entry_idx = 0;
    FAT32_DIR_ENTRY old_entry;
    int file_exists = find_directory_entry(filename,
                                            &target_lba,
                                            &target_entry_idx,
                                            &old_entry);

    if (!file_exists) {
        int end_marker = 0;
        if (!find_free_directory_entry(&target_lba,
                                       &target_entry_idx,
                                       &end_marker)) {
            return 0;
        }
        (void)end_marker;
        __builtin_memset(&old_entry, 0, sizeof(old_entry));
    }

    UINT32 clusters_needed = 0;
    if (size > 0) {
        clusters_needed = (size + bytes_per_cluster - 1U) / bytes_per_cluster;
    }

    UINT32 new_first_cluster = 0;
    if (!allocate_cluster_chain(clusters_needed, &new_first_cluster)) {
        return 0;
    }

    if (clusters_needed > 0) {
        if (!write_file_clusters(new_first_cluster,
                                 (const UINT8 *)buffer,
                                 size)) {
            free_cluster_chain(new_first_cluster);
            return 0;
        }
    }

    UINT8 sector_buf[512];
    if (!read_sata_sector(ahci_port, target_lba, 0, 1, sector_buf)) {
        free_cluster_chain(new_first_cluster);
        return 0;
    }

    FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;

    char fat_name[11];
    to_fat_name(filename, fat_name);

    __builtin_memcpy(dir[target_entry_idx].name, fat_name, 11);
    dir[target_entry_idx].attr = 0x20;
    dir[target_entry_idx].nt_res = 0;
    dir[target_entry_idx].crt_time_tenth = 0;
    dir[target_entry_idx].crt_time = 0;
    dir[target_entry_idx].crt_date = 0;
    dir[target_entry_idx].lst_acc_date = 0;
    dir[target_entry_idx].first_cluster_high = (UINT16)(new_first_cluster >> 16);
    dir[target_entry_idx].wrt_time = 0;
    dir[target_entry_idx].wrt_date = 0;
    dir[target_entry_idx].first_cluster_low = (UINT16)(new_first_cluster & 0xFFFFU);
    dir[target_entry_idx].file_size = size;

    if (!write_sata_sector(ahci_port, target_lba, 0, 1, sector_buf)) {
        free_cluster_chain(new_first_cluster);
        return 0;
    }

    if (file_exists) {
        UINT32 old_first = ((UINT32)old_entry.first_cluster_high << 16) |
                           old_entry.first_cluster_low;
        if (old_first >= 2) {
            free_cluster_chain(old_first);
        }
    }

    return 1;
}

static void fat_name_to_string(const UINT8 *fat_name, char *out) {
    int pos = 0;
    for (int i = 0; i < 8; i++) {
        if (fat_name[i] != ' ') {
            out[pos++] = fat_name[i];
        }
    }
    if (fat_name[8] != ' ') {
        out[pos++] = '.';
        for (int i = 8; i < 11; i++) {
            if (fat_name[i] != ' ') {
                out[pos++] = fat_name[i];
            }
        }
    }
    out[pos] = '\0';
}

int fat32_list_root(void *ahci_port, char out_files[][32], int max_files) {
    if (!ahci_port || !out_files || max_files <= 0) return 0;
    if (g_port != ahci_port || !g_initialized) {
        if (!fat32_init(ahci_port)) return 0;
    }

    UINT8 sector_buf[512];
    UINT32 current_cluster = bpb.root_cluster;
    int found_count = 0;
    UINT32 guard = 0;

    while (valid_cluster(current_cluster) && !is_eoc(current_cluster) && found_count < max_files) {
        if (++guard > total_clusters) break;

        UINT32 lba = cluster_to_lba(current_cluster);
        if (lba == 0) break;

        for (UINT32 s = 0; s < bpb.sectors_per_cluster && found_count < max_files; s++) {
            if (!read_sata_sector(g_port, lba + s, 0, 1, sector_buf)) return found_count;

            FAT32_DIR_ENTRY *dir = (FAT32_DIR_ENTRY *)sector_buf;
            for (UINT32 e = 0; e < 16 && found_count < max_files; e++) {
                if (dir[e].name[0] == 0x00) return found_count;
                if (!is_regular_short_entry(&dir[e])) continue;

                fat_name_to_string(dir[e].name, out_files[found_count]);
                found_count++;
            }
        }

        UINT32 next = get_next_cluster(current_cluster);
        if (is_eoc(next) || !valid_cluster(next)) break;
        current_cluster = next;
    }

    return found_count;
}
// net.c - Intel e1000 Treiber, HTTP-Ortungsclient mit sekundengenauem Offset-Parser
#include "net.h"

#define E1000_REG_CTRL     0x0000
#define E1000_REG_STATUS   0x0008
#define E1000_REG_RCTL     0x0100
#define E1000_REG_TCTL     0x0400
#define E1000_REG_RDBAL    0x2800
#define E1000_REG_RDBAH    0x2804
#define E1000_REG_RDLEN    0x2808
#define E1000_REG_RDH      0x2810
#define E1000_REG_RDT      0x2818
#define E1000_REG_TDBAL    0x3800
#define E1000_REG_TDBAH    0x3804
#define E1000_REG_TDLEN    0x3808
#define E1000_REG_TDH      0x3810
#define E1000_REG_TDT      0x3818

#define RX_NUM_DESC 32
#define TX_NUM_DESC 16

typedef struct __attribute__((packed)) {
    UINT64 address;
    UINT16 length;
    UINT16 checksum;
    UINT8  status;
    UINT8  errors;
    UINT16 special;
} RX_DESC;

typedef struct __attribute__((packed)) {
    UINT64 address;
    UINT16 length;
    UINT8  cso;
    UINT8  cmd;
    UINT8  status;
    UINT8  css;
    UINT16 special;
} TX_DESC;

static RX_DESC g_rx_descs[RX_NUM_DESC] __attribute__((aligned(16)));
static TX_DESC g_tx_descs[TX_NUM_DESC] __attribute__((aligned(16)));
static UINT8 g_rx_buffers[RX_NUM_DESC][2048] __attribute__((aligned(16)));
static UINT8 g_tx_buffers[TX_NUM_DESC][2048] __attribute__((aligned(16)));

static UINTN g_mmio_base = 0;
static UINT32 g_rx_cur = 0;
static UINT32 g_tx_cur = 0;
static NetworkState g_net = {0};

/* QEMU Gateway MAC */
static const UINT8 g_gateway_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
#define IP_API_COM_IP 0xD05F7001U // 208.95.112.1

/* TCP State */
static UINT32 g_tcp_seq = 0x12345678;
static UINT32 g_tcp_ack = 0;
static UINT16 g_local_port = 49152;
static int g_tcp_state = 0;
static int g_poll_ticks = 0;

/* Zeitzonen-Datenbank */
typedef struct {
    const char *tz_name;
    const char *abbr_winter;
    const char *abbr_summer;
    int base_offset;
} TimezoneEntry;

static const TimezoneEntry g_tz_db[] = {
    {"Europe/Berlin", "CET", "CEST", 1},
    {"Europe/Vienna", "CET", "CEST", 1},
    {"Europe/Zurich", "CET", "CEST", 1},
    {"Europe/Paris",  "CET", "CEST", 1},
    {"Europe/Rome",   "CET", "CEST", 1},
    {"Europe/Madrid", "CET", "CEST", 1},
    {"Europe/London", "GMT", "BST", 0},
    {"Europe/Dublin", "GMT", "IST", 0},
    {"America/New_York", "EST", "EDT", -5},
    {"America/Chicago",  "CST", "CDT", -6},
    {"America/Denver",   "MST", "MDT", -7},
    {"America/Los_Angeles", "PST", "PDT", -8},
    {"Asia/Tokyo",     "JST", "JST", 9},
    {"Asia/Shanghai",  "CST", "CST", 8},
    {"Asia/Singapore", "SGT", "SGT", 8},
    {"Australia/Sydney", "AEST", "AEDT", 10},
    {"UTC", "UTC", "UTC", 0}
};
#define NUM_TZ_ENTRIES (sizeof(g_tz_db)/sizeof(g_tz_db[0]))

static inline void outl(unsigned short port, unsigned int val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline unsigned int inl(unsigned short port) {
    unsigned int ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static UINT32 pci_read(UINT8 bus, UINT8 slot, UINT8 func, UINT8 offset) {
    UINT32 address = (1U << 31) | ((UINT32)bus << 16) | ((UINT32)(slot & 0x1F) << 11) | ((UINT32)(func & 0x07) << 8) | (UINT32)(offset & 0xFC);
    outl(0xCF8, address);
    return inl(0xCFC);
}

static void pci_write(UINT8 bus, UINT8 slot, UINT8 func, UINT8 offset, UINT32 val) {
    UINT32 address = (1U << 31) | ((UINT32)bus << 16) | ((UINT32)(slot & 0x1F) << 11) | ((UINT32)(func & 0x07) << 8) | (UINT32)(offset & 0xFC);
    outl(0xCF8, address);
    outl(0xCFC, val);
}

static inline void mmio_write(UINT32 reg, UINT32 val) { *(volatile UINT32*)(g_mmio_base + reg) = val; }
static inline UINT32 mmio_read(UINT32 reg) { return *(volatile UINT32*)(g_mmio_base + reg); }

static inline UINT16 swap16(UINT16 v) { return (v << 8) | (v >> 8); }
static inline UINT32 swap32(UINT32 v) { return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000); }

static int e1000_send(const void *data, UINT16 len) {
    if (!g_mmio_base || len == 0 || len > 2048) return 0;
    UINT32 cur = g_tx_cur;
    __builtin_memcpy(g_tx_buffers[cur], data, len);
    g_tx_descs[cur].address = (UINT64)(UINTN)g_tx_buffers[cur];
    g_tx_descs[cur].length = len;
    g_tx_descs[cur].cmd = (1 << 0) | (1 << 1) | (1 << 3);
    g_tx_descs[cur].status = 0;
    g_tx_cur = (g_tx_cur + 1) % TX_NUM_DESC;
    mmio_write(E1000_REG_TDT, g_tx_cur);
    return 1;
}

static UINT16 calc_csum(const UINT16 *data, int len) {
    UINT32 sum = 0;
    while (len > 1) { sum += *data++; len -= 2; }
    if (len > 0) sum += *(const UINT8*)data;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (UINT16)(~sum);
}

static void send_udp(UINT32 dst_ip, UINT16 src_port, UINT16 dst_port, const void *payload, UINT16 payload_len) {
    UINT8 packet[1514];
    __builtin_memset(packet, 0, sizeof(packet));

    if (dst_ip == 0xFFFFFFFFU) {
        for (int i = 0; i < 6; i++) packet[i] = 0xFF;
    } else {
        for (int i = 0; i < 6; i++) packet[i] = g_gateway_mac[i];
    }
    for (int i = 0; i < 6; i++) packet[6 + i] = g_net.mac[i];
    packet[12] = 0x08; packet[13] = 0x00;

    UINT8 *ip = &packet[14];
    ip[0] = 0x45; ip[8] = 64; ip[9] = 17;
    UINT16 total_ip_len = 20 + 8 + payload_len;
    *(UINT16*)&ip[2] = swap16(total_ip_len);
    *(UINT32*)&ip[12] = swap32(g_net.ip);
    *(UINT32*)&ip[16] = swap32(dst_ip);
    *(UINT16*)&ip[10] = calc_csum((UINT16*)ip, 20);

    UINT8 *udp = &packet[34];
    *(UINT16*)&udp[0] = swap16(src_port);
    *(UINT16*)&udp[2] = swap16(dst_port);
    *(UINT16*)&udp[4] = swap16(8 + payload_len);

    __builtin_memcpy(&packet[42], payload, payload_len);
    e1000_send(packet, 14 + total_ip_len);
}

static void send_tcp(UINT32 dst_ip, UINT16 src_port, UINT16 dst_port, UINT8 flags, const void *payload, UINT16 payload_len) {
    UINT8 packet[1514];
    __builtin_memset(packet, 0, sizeof(packet));

    for (int i = 0; i < 6; i++) packet[i] = g_gateway_mac[i];
    for (int i = 0; i < 6; i++) packet[6 + i] = g_net.mac[i];
    packet[12] = 0x08; packet[13] = 0x00;

    UINT8 *ip = &packet[14];
    ip[0] = 0x45; ip[8] = 64; ip[9] = 6;
    UINT16 total_ip_len = 20 + 20 + payload_len;
    *(UINT16*)&ip[2] = swap16(total_ip_len);
    *(UINT32*)&ip[12] = swap32(g_net.ip);
    *(UINT32*)&ip[16] = swap32(dst_ip);
    *(UINT16*)&ip[10] = calc_csum((UINT16*)ip, 20);

    UINT8 *tcp = &packet[34];
    *(UINT16*)&tcp[0] = swap16(src_port);
    *(UINT16*)&tcp[2] = swap16(dst_port);
    *(UINT32*)&tcp[4] = swap32(g_tcp_seq);
    *(UINT32*)&tcp[8] = swap32(g_tcp_ack);
    tcp[12] = (5 << 4);
    tcp[13] = flags;
    *(UINT16*)&tcp[14] = swap16(8192);

    if (payload && payload_len > 0) {
        __builtin_memcpy(&packet[54], payload, payload_len);
    }

    struct __attribute__((packed)) {
        UINT32 src; UINT32 dst; UINT8 zero; UINT8 proto; UINT16 len;
    } pseudo = { swap32(g_net.ip), swap32(dst_ip), 0, 6, swap16(20 + payload_len) };

    UINT32 sum = 0;
    const UINT16 *p_hdr = (const UINT16*)&pseudo;
    for (int i = 0; i < 6; i++) sum += p_hdr[i];
    const UINT16 *t_hdr = (const UINT16*)tcp;
    for (int i = 0; i < (20 + payload_len + 1) / 2; i++) sum += t_hdr[i];
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    *(UINT16*)&tcp[16] = (UINT16)(~sum);

    e1000_send(packet, 14 + total_ip_len);
}

static int extract_json_str(const char *json, const char *key, char *out, int max_len) {
    int key_len = 0; while (key[key_len]) key_len++;
    for (int i = 0; json[i] != '\0'; i++) {
        if (json[i] == '"') {
            int match = 1;
            for (int k = 0; k < key_len; k++) {
                if (json[i + 1 + k] != key[k]) { match = 0; break; }
            }
            if (match && json[i + 1 + key_len] == '"') {
                int p = i + 1 + key_len + 1;
                while (json[p] == ' ' || json[p] == ':') p++;
                if (json[p] == '"') p++;
                int out_pos = 0;
                while (json[p] && json[p] != '"' && json[p] != ',' && json[p] != '}' && out_pos < max_len - 1) {
                    out[out_pos++] = json[p++];
                }
                out[out_pos] = '\0';
                return 1;
            }
        }
    }
    return 0;
}

/* Extrahiert Integer aus JSON (auch negative Werte wie "offset": -18000) */
static int extract_json_int(const char *json, const char *key, int *out) {
    int key_len = 0; while (key[key_len]) key_len++;
    for (int i = 0; json[i] != '\0'; i++) {
        if (json[i] == '"') {
            int match = 1;
            for (int k = 0; k < key_len; k++) {
                if (json[i + 1 + k] != key[k]) { match = 0; break; }
            }
            if (match && json[i + 1 + key_len] == '"') {
                int p = i + 1 + key_len + 1;
                while (json[p] == ' ' || json[p] == ':') p++;
                int neg = 0;
                if (json[p] == '-') { neg = 1; p++; }
                int val = 0, found = 0;
                while (json[p] >= '0' && json[p] <= '9') {
                    val = val * 10 + (json[p++] - '0');
                    found = 1;
                }
                if (found) {
                    *out = neg ? -val : val;
                    return 1;
                }
            }
        }
    }
    return 0;
}

static void lookup_timezone_db(const char *tz_id, int offset_hours) {
    for (UINTN i = 0; i < NUM_TZ_ENTRIES; i++) {
        const char *t1 = tz_id;
        const char *t2 = g_tz_db[i].tz_name;
        int match = 1;
        while (*t1 && *t2) {
            if (*t1 != *t2) { match = 0; break; }
            t1++; t2++;
        }
        if (match && *t1 == *t2) {
            const char *chosen_abbr = (offset_hours > g_tz_db[i].base_offset) ? 
                                       g_tz_db[i].abbr_summer : g_tz_db[i].abbr_winter;
            int p = 0;
            while (chosen_abbr[p] && p < 7) {
                g_net.timezone_abbr[p] = chosen_abbr[p];
                p++;
            }
            g_net.timezone_abbr[p] = '\0';
            g_net.tz_offset_hours = offset_hours;
            return;
        }
    }

    // Fallback Abbr
    if (offset_hours == 1) __builtin_memcpy(g_net.timezone_abbr, "CET", 4);
    else if (offset_hours == 2) __builtin_memcpy(g_net.timezone_abbr, "CEST", 5);
    else __builtin_memcpy(g_net.timezone_abbr, "UTC", 4);
    g_net.tz_offset_hours = offset_hours;
}

void net_fetch_location_and_time(void) {
    if (!g_net.dhcp_done) return;
    g_tcp_state = 1;
    g_local_port++;
    g_tcp_seq = 0x54321000;
    g_tcp_ack = 0;

    send_tcp(IP_API_COM_IP, g_local_port, 80, 0x02, NULL, 0); // SYN
}

static void send_dhcp_discover(void) {
    UINT8 dhcp[300];
    __builtin_memset(dhcp, 0, sizeof(dhcp));
    dhcp[0] = 1; dhcp[1] = 1; dhcp[2] = 6;
    *(UINT32*)&dhcp[4] = 0x3903F326;
    for (int i = 0; i < 6; i++) dhcp[28 + i] = g_net.mac[i];
    *(UINT32*)&dhcp[236] = 0x63825363;
    dhcp[240] = 53; dhcp[241] = 1; dhcp[242] = 1;
    dhcp[243] = 255;
    send_udp(0xFFFFFFFF, 68, 67, dhcp, 244);
}

void net_poll(void) {
    if (!g_mmio_base) return;

    g_poll_ticks++;

    // Offline-Fallback nach 2 Sekunden
    if (!g_net.http_synced && g_poll_ticks > 120) {
        if (g_net.city[0] == 'E') {
            __builtin_memcpy(g_net.city, "Berlin", 7);
            __builtin_memcpy(g_net.country_code, "DE", 3);
            __builtin_memcpy(g_net.timezone_abbr, "CET", 4);
            g_net.tz_offset_hours = 1;
            g_net.http_synced = 1;
        }
    }

    while (g_rx_descs[g_rx_cur].status & 0x01) {
        UINT8 *buf = g_rx_buffers[g_rx_cur];
        UINT16 len = g_rx_descs[g_rx_cur].length;

        // 1. DHCP
        if (len >= 42 && buf[12] == 0x08 && buf[13] == 0x00 && buf[23] == 17) {
            UINT16 src_p = swap16(*(UINT16*)&buf[34]);
            if (src_p == 67 && !g_net.dhcp_done) {
                g_net.ip = swap32(*(UINT32*)&buf[58]);
                g_net.gateway = 0x0A000202;
                g_net.dhcp_done = 1;
                net_fetch_location_and_time();
            }
        }
        // 2. TCP HTTP
        else if (len >= 54 && buf[12] == 0x08 && buf[13] == 0x00 && buf[23] == 6) {
            UINT16 src_port = swap16(*(UINT16*)&buf[34]);
            UINT16 dst_port = swap16(*(UINT16*)&buf[36]);

            if (src_port == 80 && dst_port == g_local_port) {
                UINT8 flags = buf[47];
                UINT32 seq_in = swap32(*(UINT32*)&buf[38]);
                UINT32 ack_in = swap32(*(UINT32*)&buf[42]);

                if ((flags & 0x12) == 0x12 && g_tcp_state == 1) { // SYN-ACK
                    g_tcp_state = 2;
                    g_tcp_seq = ack_in;
                    g_tcp_ack = seq_in + 1;

                    send_tcp(IP_API_COM_IP, g_local_port, 80, 0x10, NULL, 0); // ACK

                    const char *http_req = "GET /json HTTP/1.1\r\nHost: ip-api.com\r\nUser-Agent: VeloOS\r\nConnection: close\r\n\r\n";
                    int req_len = 0; while (http_req[req_len]) req_len++;
                    send_tcp(IP_API_COM_IP, g_local_port, 80, 0x18, http_req, req_len);
                    g_tcp_seq += req_len;
                }
                else if (flags & 0x08) { // PSH / HTTP Response
                    const char *payload = (const char*)&buf[54];
                    
                    extract_json_str(payload, "city", g_net.city, sizeof(g_net.city));
                    extract_json_str(payload, "countryCode", g_net.country_code, sizeof(g_net.country_code));
                    extract_json_str(payload, "timezone", g_net.timezone_id, sizeof(g_net.timezone_id));

                    // Exakten Offset in Sekunden parsen
                    int offset_sec = 3600;
                    if (extract_json_int(payload, "offset", &offset_sec)) {
                        int offset_h = offset_sec / 3600;
                        lookup_timezone_db(g_net.timezone_id, offset_h);
                    } else {
                        lookup_timezone_db(g_net.timezone_id, 1);
                    }

                    if (g_net.city[0] == '\0') __builtin_memcpy(g_net.city, "Berlin", 7);
                    if (g_net.country_code[0] == '\0') __builtin_memcpy(g_net.country_code, "DE", 3);

                    g_net.http_synced = 1;
                }
            }
        }

        g_rx_descs[g_rx_cur].status = 0;
        UINT32 old_cur = g_rx_cur;
        g_rx_cur = (g_rx_cur + 1) % RX_NUM_DESC;
        mmio_write(E1000_REG_RDT, old_cur);
    }
}

void net_init(void) {
    g_mmio_base = 0;
    g_poll_ticks = 0;
    __builtin_memset(&g_net, 0, sizeof(NetworkState));
    __builtin_memcpy(g_net.city, "Ermittle...", 12);
    __builtin_memcpy(g_net.country_code, "LOC", 4);
    __builtin_memcpy(g_net.timezone_abbr, "CET", 4);
    g_net.tz_offset_hours = 1;

    for (UINT16 bus = 0; bus < 256; bus++) {
        for (UINT8 slot = 0; slot < 32; slot++) {
            for (UINT8 func = 0; func < 8; func++) {
                UINT32 id = pci_read((UINT8)bus, slot, func, 0);
                if (id == 0x100E8086U || id == 0x10048086U || id == 0x100F8086U || id == 0x153A8086U) {
                    pci_write((UINT8)bus, slot, func, 0x04, 0x0007);
                    UINT32 bar0 = pci_read((UINT8)bus, slot, func, 0x10);
                    g_mmio_base = (UINTN)(bar0 & 0xFFFFFFF0U);
                    break;
                }
            }
            if (g_mmio_base) break;
        }
        if (g_mmio_base) break;
    }

    if (!g_mmio_base) return;

    UINT32 rar_low = mmio_read(0x5400);
    UINT32 rar_high = mmio_read(0x5404);
    g_net.mac[0] = rar_low & 0xFF;
    g_net.mac[1] = (rar_low >> 8) & 0xFF;
    g_net.mac[2] = (rar_low >> 16) & 0xFF;
    g_net.mac[3] = (rar_low >> 24) & 0xFF;
    g_net.mac[4] = rar_high & 0xFF;
    g_net.mac[5] = (rar_high >> 8) & 0xFF;
    g_net.link_up = 1;

    for (int i = 0; i < RX_NUM_DESC; i++) {
        g_rx_descs[i].address = (UINT64)(UINTN)g_rx_buffers[i];
        g_rx_descs[i].status = 0;
    }
    mmio_write(E1000_REG_RDBAL, (UINT32)(UINTN)g_rx_descs);
    mmio_write(E1000_REG_RDBAH, 0);
    mmio_write(E1000_REG_RDLEN, RX_NUM_DESC * sizeof(RX_DESC));
    mmio_write(E1000_REG_RDH, 0);
    mmio_write(E1000_REG_RDT, RX_NUM_DESC - 1);
    mmio_write(E1000_REG_RCTL, (1 << 1) | (1 << 2) | (1 << 4) | (1 << 15));

    mmio_write(E1000_REG_TDBAL, (UINT32)(UINTN)g_tx_descs);
    mmio_write(E1000_REG_TDBAH, 0);
    mmio_write(E1000_REG_TDLEN, TX_NUM_DESC * sizeof(TX_DESC));
    mmio_write(E1000_REG_TDH, 0);
    mmio_write(E1000_REG_TDT, 0);
    mmio_write(E1000_REG_TCTL, (1 << 1) | (1 << 3) | (0x10 << 4) | (0x40 << 12));

    send_dhcp_discover();
}

NetworkState* net_get_state(void) {
    return &g_net;
}
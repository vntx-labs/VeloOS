#include "net.h"
#include "sched.h"

void klog(const char *s);
void *memmove(void *dest, const void *src, UINTN n);

// Forward-Deklarationen
void net_poll_unlocked(void);
void send_arp_request(UINT32 target_ip);
int  net_dhcp_run(void);

#define E1000_REG_CTRL     0x0000
#define E1000_REG_STATUS   0x0008
#define E1000_REG_EERD     0x0014
#define E1000_REG_ICR      0x00C0
#define E1000_REG_IMC      0x00D8
#define E1000_REG_RCTL     0x0100
#define E1000_REG_TCTL     0x0400
#define E1000_REG_TIPG     0x0410
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

#define DHCP_STATE_IDLE      0
#define DHCP_STATE_DISCOVER  1
#define DHCP_STATE_OFFER     2
#define DHCP_STATE_REQUEST   3
#define DHCP_STATE_ACK       4

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

static volatile RX_DESC g_rx_descs[RX_NUM_DESC] __attribute__((aligned(4096)));
static volatile TX_DESC g_tx_descs[TX_NUM_DESC] __attribute__((aligned(4096)));
static volatile UINT8   g_rx_buffers[RX_NUM_DESC][2048] __attribute__((aligned(4096)));
static volatile UINT8   g_tx_buffers[TX_NUM_DESC][2048] __attribute__((aligned(4096)));

static UINTN g_mmio_base = 0;
static UINT32 g_rx_cur = 0;
static UINT32 g_tx_cur = 0;
static NetworkState g_net = {0};
static NetSocket g_sockets[MAX_SOCKETS];

static const UINT8 g_broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const UINT8 g_gateway_mac[6]   = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
static volatile int g_net_lock = 0;

static volatile int g_dhcp_state = DHCP_STATE_IDLE;
static UINT32 g_dhcp_xid = 0x3903F326;
static UINT32 g_dhcp_offered_ip = 0;
static UINT32 g_dhcp_server_id = 0;

// ==========================================
// DNS CACHE (16 Einträge)
// ==========================================
#define DNS_CACHE_SIZE 16
typedef struct {
    char host[64];
    UINT32 ip;
} DnsCacheEntry;

static DnsCacheEntry g_dns_cache[DNS_CACHE_SIZE];
static int g_dns_cache_idx = 0;

static inline int kstrcmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return -1;
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static inline void net_acquire_lock(void) {
    while (__sync_lock_test_and_set(&g_net_lock, 1)) {
        __asm__ volatile("pause");
    }
}

static inline void net_release_lock(void) {
    __sync_lock_release(&g_net_lock);
}

static UINT16 g_local_port_alloc = 49152;
static UINT32 g_dns_resolved_ip = 0;
static volatile int g_dns_resolved = 0;
static UINT16 g_dns_trans_id = 0x1A2B;
static UINT16 g_dns_active_port = 0;

static AsyncHttpJob g_async_job = {0};

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

static UINT16 e1000_read_eeprom(UINT8 addr) {
    mmio_write(E1000_REG_EERD, 1U | ((UINT32)addr << 8));
    UINT32 val = 0;
    int guard = 0;
    while (guard++ < 50000) {
        val = mmio_read(E1000_REG_EERD);
        if (val & (1 << 4)) break;
        __asm__ volatile("pause");
    }
    return (UINT16)((val >> 16) & 0xFFFF);
}

static int e1000_send_locked(const void *data, UINT16 len) {
    if (!g_mmio_base || len == 0 || len > 2048) return 0;

    UINT32 cur = g_tx_cur;
    __builtin_memcpy((void*)g_tx_buffers[cur], data, len);

    g_tx_descs[cur].address = (UINT64)(UINTN)g_tx_buffers[cur];
    g_tx_descs[cur].length = len;
    g_tx_descs[cur].cmd = (1 << 0) | (1 << 1) | (1 << 3);
    g_tx_descs[cur].status = 0;
    g_tx_descs[cur].cso = 0;
    g_tx_descs[cur].css = 0;
    g_tx_descs[cur].special = 0;

    __asm__ volatile("" ::: "memory");

    g_tx_cur = (g_tx_cur + 1) % TX_NUM_DESC;
    mmio_write(E1000_REG_TDT, g_tx_cur);
    return 1;
}

UINT16 calc_csum(const void *data, int len) {
    if (!data || len <= 0) return 0;
    const UINT16 *ptr = (const UINT16*)data;
    UINT32 sum = 0;

    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    if (len > 0) {
        sum += *(const UINT8*)ptr;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (UINT16)(~sum);
}

void send_arp_request(UINT32 target_ip) {
    if (target_ip == 0) return;
    UINT8 packet[42];
    __builtin_memset(packet, 0, sizeof(packet));

    for (int i = 0; i < 6; i++) packet[i] = 0xFF;
    for (int i = 0; i < 6; i++) packet[6 + i] = g_net.mac[i];
    packet[12] = 0x08; packet[13] = 0x06;

    *(UINT16*)&packet[14] = swap16(1);
    *(UINT16*)&packet[16] = swap16(0x0800);
    packet[18] = 6;
    packet[19] = 4;
    *(UINT16*)&packet[20] = swap16(1);

    for (int i = 0; i < 6; i++) packet[22 + i] = g_net.mac[i];
    *(UINT32*)&packet[28] = swap32(g_net.ip);
    for (int i = 0; i < 6; i++) packet[32 + i] = 0x00;
    *(UINT32*)&packet[38] = swap32(target_ip);

    e1000_send_locked(packet, 42);
}

static void send_arp_reply(const UINT8 *target_mac, UINT32 target_ip) {
    UINT8 packet[42];
    __builtin_memset(packet, 0, sizeof(packet));

    for (int i = 0; i < 6; i++) packet[i] = target_mac[i];
    for (int i = 0; i < 6; i++) packet[6 + i] = g_net.mac[i];
    packet[12] = 0x08; packet[13] = 0x06;

    *(UINT16*)&packet[14] = swap16(1);
    *(UINT16*)&packet[16] = swap16(0x0800);
    packet[18] = 6;
    packet[19] = 4;
    *(UINT16*)&packet[20] = swap16(2);

    for (int i = 0; i < 6; i++) packet[22 + i] = g_net.mac[i];
    *(UINT32*)&packet[28] = swap32(g_net.ip);
    for (int i = 0; i < 6; i++) packet[32 + i] = target_mac[i];
    *(UINT32*)&packet[38] = swap32(target_ip);

    e1000_send_locked(packet, 42);
}

void send_udp(UINT32 dst_ip, UINT16 src_port, UINT16 dst_port, const void *payload, UINT16 payload_len) {
    UINT8 packet[1514];
    __builtin_memset(packet, 0, sizeof(packet));

    const UINT8 *dst_mac = (dst_ip == 0xFFFFFFFFU) ? g_broadcast_mac : (g_net.gateway_mac_resolved ? g_net.gateway_mac : g_gateway_mac);

    for (int i = 0; i < 6; i++) packet[i] = dst_mac[i];
    for (int i = 0; i < 6; i++) packet[6 + i] = g_net.mac[i];
    packet[12] = 0x08; packet[13] = 0x00;

    UINT8 *ip = &packet[14];
    ip[0] = 0x45;
    UINT16 total_ip_len = 20 + 8 + payload_len;
    *(UINT16*)&ip[2] = swap16(total_ip_len);
    *(UINT16*)&ip[4] = swap16(0x1337);
    ip[8] = 64;
    ip[9] = 17;
    *(UINT32*)&ip[12] = swap32(g_net.ip);
    *(UINT32*)&ip[16] = swap32(dst_ip);
    *(UINT16*)&ip[10] = calc_csum(ip, 20);

    UINT8 *udp = &packet[34];
    *(UINT16*)&udp[0] = swap16(src_port);
    *(UINT16*)&udp[2] = swap16(dst_port);
    UINT16 udp_len = 8 + payload_len;
    *(UINT16*)&udp[4] = swap16(udp_len);
    *(UINT16*)&udp[6] = 0;

    if (payload && payload_len > 0) {
        __builtin_memcpy(&packet[42], payload, payload_len);
    }

    e1000_send_locked(packet, 14 + total_ip_len);
}

void send_tcp(UINT32 dst_ip, UINT16 src_port, UINT16 dst_port, UINT32 seq, UINT32 ack, UINT8 flags, const void *payload, UINT16 payload_len) {
    UINT8 packet[1514];
    __builtin_memset(packet, 0, sizeof(packet));

    const UINT8 *dst_mac = g_net.gateway_mac_resolved ? g_net.gateway_mac : g_gateway_mac;
    for (int i = 0; i < 6; i++) packet[i] = dst_mac[i];
    for (int i = 0; i < 6; i++) packet[6 + i] = g_net.mac[i];
    packet[12] = 0x08; packet[13] = 0x00;

    UINT8 *ip = &packet[14];
    ip[0] = 0x45;
    UINT16 total_ip_len = 20 + 20 + payload_len;
    *(UINT16*)&ip[2] = swap16(total_ip_len);
    *(UINT16*)&ip[4] = swap16(0x7890);
    ip[8] = 64;
    ip[9] = 6;
    *(UINT32*)&ip[12] = swap32(g_net.ip);
    *(UINT32*)&ip[16] = swap32(dst_ip);
    *(UINT16*)&ip[10] = calc_csum(ip, 20);

    UINT8 *tcp = &packet[34];
    *(UINT16*)&tcp[0] = swap16(src_port);
    *(UINT16*)&tcp[2] = swap16(dst_port);
    *(UINT32*)&tcp[4] = swap32(seq);
    *(UINT32*)&tcp[8] = swap32(ack);
    tcp[12] = (5 << 4);
    tcp[13] = flags;
    *(UINT16*)&tcp[14] = swap16(32768);
    *(UINT16*)&tcp[16] = 0;
    *(UINT16*)&tcp[18] = 0;

    if (payload && payload_len > 0) {
        __builtin_memcpy(&packet[54], payload, payload_len);
    }

    UINT8 pseudo[12];
    *(UINT32*)&pseudo[0] = swap32(g_net.ip);
    *(UINT32*)&pseudo[4] = swap32(dst_ip);
    pseudo[8] = 0;
    pseudo[9] = 6;
    *(UINT16*)&pseudo[10] = swap16(20 + payload_len);

    UINT32 sum = 0;
    for (int i = 0; i < 12; i += 2) {
        sum += (UINT32)pseudo[i] | ((UINT32)pseudo[i + 1] << 8);
    }

    const UINT16 *p = (const UINT16*)tcp;
    int bytes = 20 + payload_len;
    while (bytes > 1) {
        sum += *p++;
        bytes -= 2;
    }
    if (bytes > 0) {
        sum += *(const UINT8*)p;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    *(UINT16*)&tcp[16] = (UINT16)(~sum);

    e1000_send_locked(packet, 14 + total_ip_len);
}

// ==========================================
// DHCP PROTOKOLL CLIENT (RFC 2131)
// ==========================================
static void send_dhcp_discover(void) {
    UINT8 dhcp[548];
    __builtin_memset(dhcp, 0, sizeof(dhcp));

    dhcp[0] = 1;
    dhcp[1] = 1;
    dhcp[2] = 6;
    dhcp[3] = 0;
    *(UINT32*)&dhcp[4] = swap32(g_dhcp_xid);
    *(UINT16*)&dhcp[10] = swap16(0x8000);
    __builtin_memcpy(&dhcp[28], g_net.mac, 6);

    dhcp[236] = 0x63; dhcp[237] = 0x82; dhcp[238] = 0x53; dhcp[239] = 0x63;

    int opt = 240;
    dhcp[opt++] = 53; dhcp[opt++] = 1; dhcp[opt++] = 1;
    
    dhcp[opt++] = 61; dhcp[opt++] = 7; dhcp[opt++] = 1;
    for (int i = 0; i < 6; i++) dhcp[opt++] = g_net.mac[i];

    dhcp[opt++] = 55; dhcp[opt++] = 3; dhcp[opt++] = 1; dhcp[opt++] = 3; dhcp[opt++] = 6;
    dhcp[opt++] = 255;

    g_dhcp_state = DHCP_STATE_DISCOVER;
    send_udp(0xFFFFFFFFU, 68, 67, dhcp, sizeof(dhcp));
}

static void send_dhcp_request(UINT32 offered_ip, UINT32 server_id) {
    UINT8 dhcp[548];
    __builtin_memset(dhcp, 0, sizeof(dhcp));

    dhcp[0] = 1; dhcp[1] = 1; dhcp[2] = 6;
    *(UINT32*)&dhcp[4] = swap32(g_dhcp_xid);
    *(UINT16*)&dhcp[10] = swap16(0x8000);
    __builtin_memcpy(&dhcp[28], g_net.mac, 6);

    dhcp[236] = 0x63; dhcp[237] = 0x82; dhcp[238] = 0x53; dhcp[239] = 0x63;

    int opt = 240;
    dhcp[opt++] = 53; dhcp[opt++] = 1; dhcp[opt++] = 3;
    
    dhcp[opt++] = 61; dhcp[opt++] = 7; dhcp[opt++] = 1;
    for (int i = 0; i < 6; i++) dhcp[opt++] = g_net.mac[i];

    dhcp[opt++] = 50; dhcp[opt++] = 4;
    *(UINT32*)&dhcp[opt] = swap32(offered_ip); opt += 4;
    
    dhcp[opt++] = 54; dhcp[opt++] = 4;
    *(UINT32*)&dhcp[opt] = swap32(server_id); opt += 4;
    
    dhcp[opt++] = 55; dhcp[opt++] = 3; dhcp[opt++] = 1; dhcp[opt++] = 3; dhcp[opt++] = 6;
    dhcp[opt++] = 255;

    g_dhcp_state = DHCP_STATE_REQUEST;
    send_udp(0xFFFFFFFFU, 68, 67, dhcp, sizeof(dhcp));
}

static void parse_dhcp_packet(const UINT8 *data, int len) {
    if (len < 240 || data[0] != 2) return;

    UINT32 xid = swap32(*(const UINT32*)&data[4]);
    if (xid != g_dhcp_xid) return;

    UINT32 yiaddr = swap32(*(const UINT32*)&data[16]);
    if (data[236] != 0x63 || data[237] != 0x82 || data[238] != 0x53 || data[239] != 0x63) return;

    UINT8 msg_type = 0;
    UINT32 subnet = 0xFFFFFF00U;
    UINT32 gateway = 0;
    UINT32 dns1 = 0;
    UINT32 dns2 = 0;
    UINT32 srv_id = 0;

    int pos = 240;
    while (pos < len && data[pos] != 255) {
        UINT8 opt = data[pos++];
        if (opt == 0) continue;
        if (pos >= len) break;
        UINT8 opt_len = data[pos++];
        if (pos + opt_len > len) break;

        if (opt == 53 && opt_len >= 1) msg_type = data[pos];
        else if (opt == 1 && opt_len >= 4) subnet = swap32(*(const UINT32*)&data[pos]);
        else if (opt == 3 && opt_len >= 4) gateway = swap32(*(const UINT32*)&data[pos]);
        else if (opt == 6 && opt_len >= 4) {
            dns1 = swap32(*(const UINT32*)&data[pos]);
            if (opt_len >= 8) dns2 = swap32(*(const UINT32*)&data[pos + 4]);
        } else if (opt == 54 && opt_len >= 4) srv_id = swap32(*(const UINT32*)&data[pos]);

        pos += opt_len;
    }

    if (msg_type == 2 && g_dhcp_state == DHCP_STATE_DISCOVER) {
        g_dhcp_offered_ip = yiaddr;
        g_dhcp_server_id = srv_id;
        send_dhcp_request(yiaddr, srv_id);
    } else if (msg_type == 5 && (g_dhcp_state == DHCP_STATE_REQUEST || g_dhcp_state == DHCP_STATE_DISCOVER)) {
        g_net.ip = yiaddr;
        g_net.subnet = subnet;
        g_net.gateway = gateway;
        if (dns1 != 0) g_net.dns_primary = dns1;
        else if (gateway != 0) g_net.dns_primary = gateway;
        if (dns2 != 0) g_net.dns_secondary = dns2;

        g_net.dhcp_done = 1;
        g_dhcp_state = DHCP_STATE_ACK;

        if (g_net.gateway != 0) {
            send_arp_request(g_net.gateway);
        }
    }
}

int net_dhcp_run(void) {
    for (int retry = 0; retry < 5; retry++) {
        g_dhcp_state = DHCP_STATE_IDLE;
        g_net.dhcp_done = 0;
        g_dhcp_xid++;

        send_dhcp_discover();

        int got_offer = 0;
        for (int t = 0; t < 20000000; t++) {
            net_poll_unlocked();
            if (g_dhcp_state == DHCP_STATE_REQUEST || g_net.dhcp_done) {
                got_offer = 1;
                break;
            }
            __asm__ volatile("pause");
        }

        if (!got_offer) continue;

        for (int t = 0; t < 20000000; t++) {
            net_poll_unlocked();
            if (g_net.dhcp_done) return 1;
            __asm__ volatile("pause");
        }
    }
    return 0;
}

// ==========================================
// DNS PARSER (RFC 1035)
// ==========================================
static int skip_dns_name_safe(const UINT8 *buf, int offset, int packet_len) {
    int guard = 0;
    while (offset < packet_len && guard++ < 128) {
        UINT8 len_byte = buf[offset];
        if (len_byte == 0) return offset + 1;
        if ((len_byte & 0xC0) == 0xC0) {
            if (offset + 2 > packet_len) return -1;
            return offset + 2;
        }
        offset += (int)len_byte + 1;
    }
    return -1;
}

static void parse_dns_response_packet(const UINT8 *dns_payload, int len) {
    if (!dns_payload || len < 12) return;

    UINT16 trans_id = swap16(*(const UINT16*)&dns_payload[0]);
    if (trans_id != g_dns_trans_id) return;

    UINT16 flags = swap16(*(const UINT16*)&dns_payload[2]);
    int is_response = (flags >> 15) & 1;
    int rcode = flags & 0x0F;
    if (!is_response || rcode != 0) return;

    UINT16 qdcount = swap16(*(const UINT16*)&dns_payload[4]);
    UINT16 ancount = swap16(*(const UINT16*)&dns_payload[6]);

    int pos = 12;
    for (int q = 0; q < qdcount; q++) {
        pos = skip_dns_name_safe(dns_payload, pos, len);
        if (pos == -1 || pos + 4 > len) return;
        pos += 4;
    }

    for (int a = 0; a < ancount; a++) {
        pos = skip_dns_name_safe(dns_payload, pos, len);
        if (pos == -1 || pos + 10 > len) return;

        UINT16 type     = swap16(*(const UINT16*)&dns_payload[pos]);
        UINT16 rdlength = swap16(*(const UINT16*)&dns_payload[pos + 8]);
        
        pos += 10; 
        if (pos + rdlength > len) return;

        if (type == 1 && rdlength == 4) {
            UINT32 ip_val = swap32(*(const UINT32*)&dns_payload[pos]);
            if (ip_val != 0) {
                g_dns_resolved_ip = ip_val;
                g_dns_resolved = 1;
                return;
            }
        }
        pos += rdlength;
    }
}

void net_poll_unlocked(void) {
    if (!g_mmio_base) return;

    __asm__ volatile("" ::: "memory");

    while (g_rx_descs[g_rx_cur].status & 0x01) {
        UINT8 *buf = (UINT8*)g_rx_buffers[g_rx_cur];
        UINT16 len = g_rx_descs[g_rx_cur].length;

        // 1. ARP
        if (len >= 42 && buf[12] == 0x08 && buf[13] == 0x06) {
            UINT16 op = swap16(*(UINT16*)&buf[20]);
            UINT32 sender_ip = swap32(*(UINT32*)&buf[28]);
            UINT32 target_ip = swap32(*(UINT32*)&buf[38]);

            if (sender_ip == g_net.gateway && sender_ip != 0) {
                __builtin_memcpy(g_net.gateway_mac, &buf[22], 6);
                g_net.gateway_mac_resolved = 1;
            }

            if (op == 1 && target_ip == g_net.ip && g_net.ip != 0) {
                send_arp_reply(&buf[22], sender_ip);
            }
        }
        // 2. UDP
        else if (len >= 42 && buf[12] == 0x08 && buf[13] == 0x00 && buf[23] == 17) {
            UINT16 src_p = swap16(*(UINT16*)&buf[34]);

            if (src_p == 67 && (len - 42) >= 240) {
                parse_dhcp_packet(&buf[42], len - 42);
            }
            else if (src_p == 53 && len >= 54) {
                parse_dns_response_packet(&buf[42], len - 42);
            }
        }
        // 3. TCP
        else if (len >= 54 && buf[12] == 0x08 && buf[13] == 0x00 && buf[23] == 6) {
            UINT16 src_p = swap16(*(UINT16*)&buf[34]);
            UINT16 dst_p = swap16(*(UINT16*)&buf[36]);
            UINT32 seq_in = swap32(*(UINT32*)&buf[38]);
            UINT32 ack_in = swap32(*(UINT32*)&buf[42]);
            UINT8  flags = buf[47];
            UINT16 ip_total_len = swap16(*(UINT16*)&buf[16]);
            UINT16 tcp_hdr_len = (buf[46] >> 4) * 4;
            UINT16 payload_len = ip_total_len > (20 + tcp_hdr_len) ? (ip_total_len - 20 - tcp_hdr_len) : 0;

            for (int s = 0; s < MAX_SOCKETS; s++) {
                NetSocket *sock = &g_sockets[s];
                if (sock->state != SOCK_FREE && sock->local_port == dst_p && sock->remote_port == src_p) {
                    if (flags & 0x04) {
                        sock->state = SOCK_FREE;
                    }
                    else if ((flags & 0x12) == 0x12 && sock->state == SOCK_CONNECTING) {
                        sock->seq = ack_in;
                        sock->ack = seq_in + 1;
                        sock->state = SOCK_ESTABLISHED;
                        send_tcp(sock->remote_ip, sock->local_port, sock->remote_port, sock->seq, sock->ack, 0x10, NULL, 0);
                    }
                    else if (payload_len > 0) {
                        sock->ack = seq_in + payload_len;
                        if (sock->rx_len + payload_len <= sizeof(sock->rx_buffer)) {
                            __builtin_memcpy(&sock->rx_buffer[sock->rx_len], &buf[14 + 20 + tcp_hdr_len], payload_len);
                            sock->rx_len += payload_len;
                        }
                        send_tcp(sock->remote_ip, sock->local_port, sock->remote_port, sock->seq, sock->ack, 0x10, NULL, 0);
                    }
                    else if (flags & 0x01) {
                        sock->ack = seq_in + 1;
                        send_tcp(sock->remote_ip, sock->local_port, sock->remote_port, sock->seq, sock->ack, 0x11, NULL, 0);
                        sock->state = SOCK_CLOSING;
                    }
                }
            }
        }

        g_rx_descs[g_rx_cur].status = 0;
        UINT32 old_cur = g_rx_cur;
        g_rx_cur = (g_rx_cur + 1) % RX_NUM_DESC;
        mmio_write(E1000_REG_RDT, old_cur);

        __asm__ volatile("" ::: "memory");
    }
}

void net_poll(void) {
    net_acquire_lock();
    net_poll_unlocked();
    net_release_lock();
}

static UINT32 send_dns_query_to_server(UINT32 dns_server_ip, const char *hostname) {
    if (!dns_server_ip || !hostname || !hostname[0]) return 0;
    g_dns_resolved = 0;
    g_dns_resolved_ip = 0;
    g_dns_trans_id++;

    g_dns_active_port = g_local_port_alloc++;
    if (g_local_port_alloc > 60000) g_local_port_alloc = 49152;

    if (!g_net.gateway_mac_resolved && g_net.gateway != 0) {
        send_arp_request(g_net.gateway);
    }

    UINT8 query[512];
    __builtin_memset(query, 0, sizeof(query));

    *(UINT16*)&query[0] = swap16(g_dns_trans_id);
    *(UINT16*)&query[2] = swap16(0x0100);
    *(UINT16*)&query[4] = swap16(1);

    int qpos = 12;
    const char *p = hostname;
    while (*p) {
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        int len = (int)(dot - p);
        if (len > 0) {
            query[qpos++] = (UINT8)len;
            for (int k = 0; k < len; k++) query[qpos++] = (UINT8)p[k];
        }
        p = *dot ? dot + 1 : dot;
    }
    query[qpos++] = 0;

    *(UINT16*)&query[qpos] = swap16(1); qpos += 2;
    *(UINT16*)&query[qpos] = swap16(1); qpos += 2;

    send_udp(dns_server_ip, g_dns_active_port, 53, query, (UINT16)qpos);

    for (int t = 0; t < 8000000; t++) {
        net_poll_unlocked();
        if (g_dns_resolved && g_dns_resolved_ip != 0) {
            g_dns_active_port = 0;
            return g_dns_resolved_ip;
        }
        __asm__ volatile("pause");
    }

    g_dns_active_port = 0;
    return 0;
}

static UINT32 net_dns_resolve_unlocked(const char *hostname) {
    if (!hostname || !hostname[0]) return 0;

    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (g_dns_cache[i].ip != 0 && kstrcmp(g_dns_cache[i].host, hostname) == 0) {
            return g_dns_cache[i].ip;
        }
    }

    if (kstrcmp(hostname, "neverssl.com") == 0 || kstrcmp(hostname, "www.neverssl.com") == 0) return 0x22F4343EU;
    if (kstrcmp(hostname, "info.cern.ch") == 0 || kstrcmp(hostname, "www.info.cern.ch") == 0) return 0xBCB8437FU;
    if (kstrcmp(hostname, "example.com") == 0 || kstrcmp(hostname, "www.example.com") == 0) return 0x5DB8D822U;

    int is_ip = 1, dots = 0;
    for (int i = 0; hostname[i]; i++) {
        if (hostname[i] == '.') dots++;
        else if (hostname[i] < '0' || hostname[i] > '9') { is_ip = 0; break; }
    }
    if (is_ip && dots == 3) {
        UINT32 b1 = 0, b2 = 0, b3 = 0, b4 = 0;
        const char *s = hostname;
        while (*s >= '0' && *s <= '9') b1 = b1 * 10 + (*s++ - '0');
        if (*s == '.') s++;
        while (*s >= '0' && *s <= '9') b2 = b2 * 10 + (*s++ - '0');
        if (*s == '.') s++;
        while (*s >= '0' && *s <= '9') b3 = b3 * 10 + (*s++ - '0');
        if (*s == '.') s++;
        while (*s >= '0' && *s <= '9') b4 = b4 * 10 + (*s++ - '0');
        return (b1 << 24) | (b2 << 16) | (b3 << 8) | b4;
    }

    UINT32 ip = 0;
    if (g_net.dns_primary != 0) {
        ip = send_dns_query_to_server(g_net.dns_primary, hostname);
    }
    if (ip == 0 && g_net.gateway != 0) {
        ip = send_dns_query_to_server(g_net.gateway, hostname);
    }
    if (ip == 0) {
        ip = send_dns_query_to_server(0x01010101U, hostname);
    }
    if (ip == 0) {
        ip = send_dns_query_to_server(0x08080808U, hostname);
    }

    if (ip != 0) {
        int idx = g_dns_cache_idx;
        int p = 0;
        while (hostname[p] && p < 63) { g_dns_cache[idx].host[p] = hostname[p]; p++; }
        g_dns_cache[idx].host[p] = '\0';
        g_dns_cache[idx].ip = ip;
        g_dns_cache_idx = (g_dns_cache_idx + 1) % DNS_CACHE_SIZE;
    }

    return ip;
}

UINT32 net_dns_resolve(const char *hostname) {
    net_acquire_lock();
    UINT32 ip = net_dns_resolve_unlocked(hostname);
    net_release_lock();
    return ip;
}

// ==========================================
// SYNCHRONE USERLAND SOCKET IMPLEMENTIERUNG
// ==========================================
int net_socket_open(const char *host, UINT16 port, int use_tls) {
    (void)use_tls;
    if (!host || !host[0]) return -1;

    net_acquire_lock();
    UINT32 ip = net_dns_resolve_unlocked(host);
    if (ip == 0) {
        net_release_lock();
        return -1;
    }

    int slot = -1;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (g_sockets[i].state == SOCK_FREE) { slot = i; break; }
    }
    if (slot == -1) {
        net_release_lock();
        return -1;
    }

    NetSocket *s = &g_sockets[slot];
    __builtin_memset(s, 0, sizeof(NetSocket));
    s->id = slot;
    s->state = SOCK_CONNECTING;
    s->remote_ip = ip;
    s->remote_port = port;
    s->local_port = g_local_port_alloc++;
    if (g_local_port_alloc > 60000) g_local_port_alloc = 49152;
    s->seq = 0x50000000U + (slot * 0x10000U);
    s->ack = 0;

    send_tcp(s->remote_ip, s->local_port, s->remote_port, s->seq, 0, 0x02, NULL, 0);

    for (int t = 0; t < 10000000; t++) {
        net_poll_unlocked();
        if (s->state == SOCK_ESTABLISHED) {
            net_release_lock();
            return slot;
        }
        __asm__ volatile("pause");
    }

    s->state = SOCK_FREE;
    net_release_lock();
    return -1;
}

int net_socket_send(int sock_id, const void *data, int len) {
    if (sock_id < 0 || sock_id >= MAX_SOCKETS || !data || len <= 0) return -1;
    net_acquire_lock();
    NetSocket *s = &g_sockets[sock_id];
    if (s->state != SOCK_ESTABLISHED) {
        net_release_lock();
        return -1;
    }

    send_tcp(s->remote_ip, s->local_port, s->remote_port, s->seq, s->ack, 0x18, data, (UINT16)len);
    s->seq += len;
    net_release_lock();
    return len;
}

int net_socket_recv(int sock_id, void *buf, int max_len) {
    if (sock_id < 0 || sock_id >= MAX_SOCKETS || !buf || max_len <= 0) return -1;
    net_acquire_lock();
    net_poll_unlocked();
    NetSocket *s = &g_sockets[sock_id];

    if (s->rx_len > s->rx_read_pos) {
        UINT32 avail = s->rx_len - s->rx_read_pos;
        UINT32 chunk = avail < (UINT32)max_len ? avail : (UINT32)max_len;
        __builtin_memcpy(buf, &s->rx_buffer[s->rx_read_pos], chunk);
        s->rx_read_pos += chunk;
        net_release_lock();
        return (int)chunk;
    }

    net_release_lock();
    return 0;
}

void net_socket_close(int sock_id) {
    if (sock_id < 0 || sock_id >= MAX_SOCKETS) return;
    net_acquire_lock();
    NetSocket *s = &g_sockets[sock_id];
    if (s->state != SOCK_FREE) {
        send_tcp(s->remote_ip, s->local_port, s->remote_port, s->seq, s->ack, 0x11, NULL, 0);
        s->state = SOCK_FREE;
    }
    net_release_lock();
}

int net_http_get(const char *url, char *out_buf, int max_len) {
    if (!url || !out_buf || max_len <= 0) return 0;
    if (!net_http_async_start(url)) return 0;

    int status = 0, bytes = 0;
    for (int t = 0; t < 1000; t++) {
        int res = net_http_async_poll(out_buf, max_len, &status, &bytes);
        if (res > 0) return res;
        if (status == HTTP_STATUS_ERROR) break;
        task_sleep(1);
    }
    return 0;
}

// ==========================================
// ASYNCHRONE HTTP ENGINE (Instant Cancel & Safe Reset)
// ==========================================
int net_http_async_start(const char *url) {
    if (!url || !url[0]) return 0;

    net_acquire_lock();

    // Laufenden Socket sofort freigeben und abbrechen
    if (g_async_job.sock_id >= 0 && g_async_job.sock_id < MAX_SOCKETS) {
        NetSocket *old_s = &g_sockets[g_async_job.sock_id];
        if (old_s->state != SOCK_FREE) {
            send_tcp(old_s->remote_ip, old_s->local_port, old_s->remote_port, old_s->seq, old_s->ack, 0x14, NULL, 0); // RST/ACK
            old_s->state = SOCK_FREE;
        }
    }

    __builtin_memset(&g_async_job, 0, sizeof(AsyncHttpJob));

    const char *u = url;
    while (*u == ' ' || *u == '\t' || *u == '\n' || *u == '\r') u++;

    int ulen = 0;
    while (u[ulen] && u[ulen] != ' ' && u[ulen] != '\r' && u[ulen] != '\n' && ulen < 254) {
        g_async_job.url[ulen] = u[ulen];
        ulen++;
    }
    g_async_job.url[ulen] = '\0';

    const char *host_start = g_async_job.url;
    if (host_start[0] == 'h' && host_start[1] == 't' && host_start[2] == 't' && host_start[3] == 'p' && host_start[4] == ':') {
        host_start += 7;
    } else if (host_start[0] == 'h' && host_start[1] == 't' && host_start[2] == 't' && host_start[3] == 'p' && host_start[4] == 's' && host_start[5] == ':') {
        host_start += 8;
    }

    g_async_job.port = 80;
    const char *slash = host_start;
    while (*slash && *slash != '/' && *slash != ':') slash++;

    int hlen = (int)(slash - host_start);
    if (hlen > 63) hlen = 63;
    __builtin_memcpy(g_async_job.host, host_start, hlen);
    g_async_job.host[hlen] = '\0';

    if (*slash == ':') {
        slash++;
        g_async_job.port = 0;
        while (*slash >= '0' && *slash <= '9') {
            g_async_job.port = g_async_job.port * 10 + (*slash - '0');
            slash++;
        }
    }

    if (*slash == '/') {
        int plen = 0;
        while (slash[plen] && plen < 127) { g_async_job.path[plen] = slash[plen]; plen++; }
        g_async_job.path[plen] = '\0';
    } else {
        g_async_job.path[0] = '/'; g_async_job.path[1] = '\0';
    }

    g_async_job.sock_id = -1;
    g_async_job.state_step = 0;
    g_async_job.status = HTTP_STATUS_PENDING;
    g_async_job.timeout_ticks = 4000;

    net_release_lock();
    return 1;
}

int net_http_async_poll(char *out_buf, int max_len, int *out_status, int *out_bytes) {
    net_acquire_lock();
    net_poll_unlocked();

    if (g_async_job.status == HTTP_STATUS_PENDING) {
        if (g_async_job.timeout_ticks > 0) {
            g_async_job.timeout_ticks--;
        } else {
            g_async_job.status = HTTP_STATUS_ERROR;
        }

        if (g_async_job.state_step == 0) {
            if (!g_net.dhcp_done) {
                net_dhcp_run();
            }

            g_async_job.ip = net_dns_resolve_unlocked(g_async_job.host);
            if (g_async_job.ip != 0) {
                int slot = -1;
                for (int i = 0; i < MAX_SOCKETS; i++) {
                    if (g_sockets[i].state == SOCK_FREE) { slot = i; break; }
                }
                if (slot != -1) {
                    NetSocket *s = &g_sockets[slot];
                    __builtin_memset(s, 0, sizeof(NetSocket));
                    s->id = slot;
                    s->state = SOCK_CONNECTING;
                    s->remote_ip = g_async_job.ip;
                    s->remote_port = g_async_job.port;
                    s->local_port = g_local_port_alloc++;
                    if (g_local_port_alloc > 60000) g_local_port_alloc = 49152;
                    s->seq = 0x50000000U + (slot * 0x10000U);
                    s->ack = 0;

                    send_tcp(s->remote_ip, s->local_port, s->remote_port, s->seq, 0, 0x02, NULL, 0);
                    g_async_job.sock_id = slot;
                    g_async_job.state_step = 1;
                } else {
                    g_async_job.status = HTTP_STATUS_ERROR;
                }
            }
        }
        else if (g_async_job.state_step == 1) {
            NetSocket *s = &g_sockets[g_async_job.sock_id];
            if (s->state == SOCK_ESTABLISHED) {
                char req[512];
                int req_len = 0;
                const char *p1 = "GET "; while (*p1) req[req_len++] = *p1++;
                const char *p2 = g_async_job.path; while (*p2) req[req_len++] = *p2++;
                const char *p3 = " HTTP/1.1\r\nHost: "; while (*p3) req[req_len++] = *p3++;
                const char *p4 = g_async_job.host; while (*p4) req[req_len++] = *p4++;
                const char *p5 = "\r\nUser-Agent: VeloFox/1.0\r\nAccept: */*\r\nConnection: close\r\n\r\n";
                while (*p5) req[req_len++] = *p5++;

                send_tcp(s->remote_ip, s->local_port, s->remote_port, s->seq, s->ack, 0x18, req, (UINT16)req_len);
                s->seq += req_len;
                g_async_job.state_step = 2;
            }
        }
        else if (g_async_job.state_step == 2) {
            NetSocket *s = &g_sockets[g_async_job.sock_id];
            if (s->rx_len > s->rx_read_pos) {
                UINT32 avail = s->rx_len - s->rx_read_pos;
                UINT32 rem = sizeof(g_async_job.buffer) - 1 - g_async_job.bytes_read;
                UINT32 chunk = avail < rem ? avail : rem;
                __builtin_memcpy(&g_async_job.buffer[g_async_job.bytes_read], &s->rx_buffer[s->rx_read_pos], chunk);
                s->rx_read_pos += chunk;
                g_async_job.bytes_read += chunk;
            }

            if (s->state == SOCK_CLOSING || s->state == SOCK_FREE || (g_async_job.bytes_read > 0 && s->rx_read_pos >= s->rx_len)) {
                char *body = NULL;
                for (int i = 0; i < g_async_job.bytes_read - 3; i++) {
                    if (g_async_job.buffer[i] == '\r' && g_async_job.buffer[i+1] == '\n' &&
                        g_async_job.buffer[i+2] == '\r' && g_async_job.buffer[i+3] == '\n') {
                        body = (char*)&g_async_job.buffer[i + 4];
                        break;
                    }
                }
                if (body) {
                    int body_len = g_async_job.bytes_read - (int)(body - (char*)g_async_job.buffer);
                    memmove(g_async_job.buffer, body, (UINTN)body_len);
                    g_async_job.bytes_read = body_len;
                }
                g_async_job.buffer[g_async_job.bytes_read] = '\0';
                g_async_job.status = HTTP_STATUS_READY;
                s->state = SOCK_FREE;
            }
        }
    }

    if (out_status) *out_status = g_async_job.status;
    if (out_bytes) *out_bytes = (g_async_job.state_step << 24) | (g_async_job.bytes_read & 0xFFFFFF);

    int ret = 0;
    if (g_async_job.status == HTTP_STATUS_READY && out_buf && max_len > 0) {
        int copy_len = g_async_job.bytes_read < (max_len - 1) ? g_async_job.bytes_read : (max_len - 1);
        __builtin_memcpy(out_buf, g_async_job.buffer, copy_len);
        out_buf[copy_len] = '\0';
        g_async_job.status = HTTP_STATUS_IDLE;
        ret = copy_len;
    }

    net_release_lock();
    return ret;
}

void net_init(void) {
    g_mmio_base = 0;
    g_net_lock = 0;
    g_rx_cur = 0;
    g_tx_cur = 0;
    g_dns_cache_idx = 0;

    __builtin_memset(&g_net, 0, sizeof(NetworkState));
    __builtin_memset(g_sockets, 0, sizeof(g_sockets));
    __builtin_memset(&g_async_job, 0, sizeof(AsyncHttpJob));
    __builtin_memset(g_dns_cache, 0, sizeof(g_dns_cache));

    for (UINT16 bus = 0; bus < 256; bus++) {
        for (UINT8 slot = 0; slot < 32; slot++) {
            for (UINT8 func = 0; func < 8; func++) {
                UINT32 id = pci_read((UINT8)bus, slot, func, 0x00);
                if ((id & 0xFFFF) == 0x8086) {
                    UINT32 class_rev = pci_read((UINT8)bus, slot, func, 0x08);
                    UINT8 base_class = (class_rev >> 24) & 0xFF;
                    if (base_class == 0x02) {
                        UINT32 cmd = pci_read((UINT8)bus, slot, func, 0x04);
                        pci_write((UINT8)bus, slot, func, 0x04, cmd | 0x0007);

                        UINT32 bar0_low = pci_read((UINT8)bus, slot, func, 0x10);
                        UINT64 bar0 = (UINT64)(bar0_low & 0xFFFFFFF0U);
                        if ((bar0_low & 0x6U) == 0x4U) {
                            UINT32 bar0_high = pci_read((UINT8)bus, slot, func, 0x14);
                            bar0 |= ((UINT64)bar0_high << 32);
                        }
                        g_mmio_base = (UINTN)(bar0 & 0xFFFFFFFFFFFFFFF0ULL);
                        break;
                    }
                }
            }
            if (g_mmio_base) break;
        }
        if (g_mmio_base) break;
    }

    if (!g_mmio_base) return;

    mmio_write(E1000_REG_IMC, 0xFFFFFFFF);
    mmio_read(E1000_REG_ICR);

    UINT32 ctrl = mmio_read(E1000_REG_CTRL);
    ctrl |= (1 << 5) | (1 << 6);
    ctrl &= ~(1 << 3);
    mmio_write(E1000_REG_CTRL, ctrl);

    UINT32 rar_low = mmio_read(0x5400);
    UINT32 rar_high = mmio_read(0x5404);
    g_net.mac[0] = rar_low & 0xFF;
    g_net.mac[1] = (rar_low >> 8) & 0xFF;
    g_net.mac[2] = (rar_low >> 16) & 0xFF;
    g_net.mac[3] = (rar_low >> 24) & 0xFF;
    g_net.mac[4] = rar_high & 0xFF;
    g_net.mac[5] = (rar_high >> 8) & 0xFF;

    if (g_net.mac[0] == 0 && g_net.mac[1] == 0 && g_net.mac[2] == 0 &&
        g_net.mac[3] == 0 && g_net.mac[4] == 0 && g_net.mac[5] == 0) {
        UINT16 w0 = e1000_read_eeprom(0);
        UINT16 w1 = e1000_read_eeprom(1);
        UINT16 w2 = e1000_read_eeprom(2);
        g_net.mac[0] = w0 & 0xFF; g_net.mac[1] = w0 >> 8;
        g_net.mac[2] = w1 & 0xFF; g_net.mac[3] = w1 >> 8;
        g_net.mac[4] = w2 & 0xFF; g_net.mac[5] = w2 >> 8;
    }

    if (g_net.mac[0] == 0 && g_net.mac[1] == 0 && g_net.mac[2] == 0 &&
        g_net.mac[3] == 0 && g_net.mac[4] == 0 && g_net.mac[5] == 0) {
        g_net.mac[0] = 0x52; g_net.mac[1] = 0x54; g_net.mac[2] = 0x00;
        g_net.mac[3] = 0x12; g_net.mac[4] = 0x34; g_net.mac[5] = 0x57;
    }

    mmio_write(0x5400, *(UINT32*)&g_net.mac[0]);
    mmio_write(0x5404, (UINT32)*(UINT16*)&g_net.mac[4] | 0x80000000U);

    g_net.link_up = 1;

    for (int i = 0; i < RX_NUM_DESC; i++) {
        g_rx_descs[i].address = (UINT64)(UINTN)g_rx_buffers[i];
        g_rx_descs[i].status = 0;
    }
    mmio_write(E1000_REG_RDBAL, (UINT32)(UINTN)g_rx_descs);
    mmio_write(E1000_REG_RDBAH, (UINT32)((UINT64)(UINTN)g_rx_descs >> 32));
    mmio_write(E1000_REG_RDLEN, RX_NUM_DESC * sizeof(RX_DESC));
    mmio_write(E1000_REG_RDH, 0);
    mmio_write(E1000_REG_RDT, RX_NUM_DESC - 1);
    
    mmio_write(E1000_REG_RCTL, (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 15));

    for (int i = 0; i < TX_NUM_DESC; i++) {
        g_tx_descs[i].address = (UINT64)(UINTN)g_tx_buffers[i];
        g_tx_descs[i].status = 0;
        g_tx_descs[i].cmd = 0;
    }
    mmio_write(E1000_REG_TDBAL, (UINT32)(UINTN)g_tx_descs);
    mmio_write(E1000_REG_TDBAH, (UINT32)((UINT64)(UINTN)g_tx_descs >> 32));
    mmio_write(E1000_REG_TDLEN, TX_NUM_DESC * sizeof(TX_DESC));
    mmio_write(E1000_REG_TDH, 0);
    mmio_write(E1000_REG_TDT, 0);

    mmio_write(E1000_REG_TCTL, (1 << 1) | (1 << 3) | (0x0F << 4) | (0x40 << 12));
    mmio_write(E1000_REG_TIPG, 0x0060200A);

    net_dhcp_run();
}

NetworkState* net_get_state(void) {
    return &g_net;
}
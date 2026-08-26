#include "net.h"
#include "sched.h"

void klog(const char *s);

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
#define E1000_REG_ICR      0x00C0
#define E1000_REG_IMC      0x00D8

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
static NetSocket g_sockets[MAX_SOCKETS];

static const UINT8 g_gateway_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
static UINT16 g_local_port_alloc = 49152;
static UINT32 g_dns_resolved_ip = 0;
static int g_dns_resolved = 0;

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

// ==========================================
// KRYPTOGRAFIE: SHA-256
// ==========================================
static const UINT32 K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define Ch(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define Maj(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define S0(x) (ROR(x, 2) ^ ROR(x, 13) ^ ROR(x, 22))
#define S1(x) (ROR(x, 6) ^ ROR(x, 11) ^ ROR(x, 25))
#define s0(x) (ROR(x, 7) ^ ROR(x, 18) ^ ((x) >> 3))
#define s1(x) (ROR(x, 17) ^ ROR(x, 19) ^ ((x) >> 10))

static void __attribute__((unused)) sha256(const UINT8 *data, UINTN len, UINT8 *hash) {
    UINT32 h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    UINT8 buf[64];
    UINTN i = 0;

    while (i + 64 <= len) {
        UINT32 w[64];
        for (int t = 0; t < 16; t++) w[t] = swap32(*(UINT32*)&data[i + t * 4]);
        for (int t = 16; t < 64; t++) w[t] = s1(w[t-2]) + w[t-7] + s0(w[t-15]) + w[t-16];
        UINT32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], h_val = h[7];
        for (int t = 0; t < 64; t++) {
            UINT32 T1 = h_val + S1(e) + Ch(e, f, g) + K256[t] + w[t];
            UINT32 T2 = S0(a) + Maj(a, b, c);
            h_val = g; g = f; f = e; e = d + T1; d = c; c = b; b = a; a = T1 + T2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += h_val;
        i += 64;
    }

    __builtin_memset(buf, 0, 64);
    UINTN rem = len - i;
    __builtin_memcpy(buf, &data[i], rem);
    buf[rem] = 0x80;
    if (rem >= 56) {
        UINT32 w[64];
        for (int t = 0; t < 16; t++) w[t] = swap32(*(UINT32*)&buf[t * 4]);
        for (int t = 16; t < 64; t++) w[t] = s1(w[t-2]) + w[t-7] + s0(w[t-15]) + w[t-16];
        UINT32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], h_val = h[7];
        for (int t = 0; t < 64; t++) {
            UINT32 T1 = h_val + S1(e) + Ch(e, f, g) + K256[t] + w[t];
            UINT32 T2 = S0(a) + Maj(a, b, c);
            h_val = g; g = f; f = e; e = d + T1; d = c; c = b; b = a; a = T1 + T2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += h_val;
        __builtin_memset(buf, 0, 64);
    }

    UINT64 bits = (UINT64)len * 8ULL;
    *(UINT64*)&buf[56] = ((bits >> 56) & 0xFF) | ((bits >> 40) & 0xFF00) | ((bits >> 24) & 0xFF0000) |
                         ((bits >> 8) & 0xFF000000) | ((bits << 8) & 0xFF00000000ULL) |
                         ((bits << 24) & 0xFF0000000000ULL) | ((bits << 40) & 0xFF000000000000ULL) | (bits << 56);
    UINT32 w[64];
    for (int t = 0; t < 16; t++) w[t] = swap32(*(UINT32*)&buf[t * 4]);
    for (int t = 16; t < 64; t++) w[t] = s1(w[t-2]) + w[t-7] + s0(w[t-15]) + w[t-16];
    UINT32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], h_val = h[7];
    for (int t = 0; t < 64; t++) {
        UINT32 T1 = h_val + S1(e) + Ch(e, f, g) + K256[t] + w[t];
        UINT32 T2 = S0(a) + Maj(a, b, c);
        h_val = g; g = f; f = e; e = d + T1; d = c; c = b; b = a; a = T1 + T2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += h_val;

    for (int t = 0; t < 8; t++) *(UINT32*)&hash[t * 4] = swap32(h[t]);
}

// ==========================================
// UDP & TCP STACK MIT MULTI-SOCKETS
// ==========================================
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

static void send_tcp(UINT32 dst_ip, UINT16 src_port, UINT16 dst_port, UINT32 seq, UINT32 ack, UINT8 flags, const void *payload, UINT16 payload_len) {
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
    *(UINT32*)&tcp[4] = swap32(seq);
    *(UINT32*)&tcp[8] = swap32(ack);
    tcp[12] = (5 << 4);
    tcp[13] = flags;
    *(UINT16*)&tcp[14] = swap16(16384);

    if (payload && payload_len > 0) {
        __builtin_memcpy(&packet[54], payload, payload_len);
    }

    UINT8 pseudo_buf[12];
    *(UINT32*)&pseudo_buf[0] = swap32(g_net.ip);
    *(UINT32*)&pseudo_buf[4] = swap32(dst_ip);
    pseudo_buf[8] = 0;
    pseudo_buf[9] = 6;
    *(UINT16*)&pseudo_buf[10] = swap16(20 + payload_len);

    UINT32 sum = 0;
    const UINT16 *p_hdr = (const UINT16*)pseudo_buf;
    for (int i = 0; i < 6; i++) sum += p_hdr[i];
    const UINT16 *t_hdr = (const UINT16*)tcp;
    for (int i = 0; i < (20 + payload_len + 1) / 2; i++) sum += t_hdr[i];
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    *(UINT16*)&tcp[16] = (UINT16)(~sum);

    e1000_send(packet, 14 + total_ip_len);
}

// ==========================================
// DNS RESOLVER (PORT 53)
// ==========================================
UINT32 net_dns_resolve(const char *hostname) {
    if (!hostname || !g_net.dns) return 0;

    g_dns_resolved = 0;
    g_dns_resolved_ip = 0;

    UINT8 query[512];
    __builtin_memset(query, 0, sizeof(query));

    *(UINT16*)&query[0] = swap16(0x1234);
    *(UINT16*)&query[2] = swap16(0x0100);
    *(UINT16*)&query[4] = swap16(1);

    int qpos = 12;
    const char *p = hostname;
    while (*p) {
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        int len = (int)(dot - p);
        query[qpos++] = (UINT8)len;
        for (int k = 0; k < len; k++) query[qpos++] = p[k];
        p = *dot ? dot + 1 : dot;
    }
    query[qpos++] = 0;

    *(UINT16*)&query[qpos] = swap16(1); qpos += 2;
    *(UINT16*)&query[qpos] = swap16(1); qpos += 2;

    send_udp(g_net.dns, 54321, 53, query, (UINT16)qpos);

    for (int t = 0; t < 200; t++) {
        net_poll();
        if (g_dns_resolved) return g_dns_resolved_ip;
        task_sleep(1);
    }
    return 0;
}

// ==========================================
// SOCKET & HTTP IMPLEMENTIERUNG
// ==========================================
int net_socket_open(const char *host, UINT16 port, int use_tls) {
    UINT32 ip = net_dns_resolve(host);
    if (!ip) return -1;

    int slot = -1;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (g_sockets[i].state == SOCK_FREE) { slot = i; break; }
    }
    if (slot == -1) return -1;

    NetSocket *s = &g_sockets[slot];
    __builtin_memset(s, 0, sizeof(NetSocket));
    s->id = slot;
    s->state = SOCK_CONNECTING;
    s->is_tls = use_tls;
    s->remote_ip = ip;
    s->remote_port = port;
    s->local_port = g_local_port_alloc++;
    s->seq = 0x10000000 + (slot * 0x10000);
    s->ack = 0;
    int p = 0; while (host && host[p] && p < 63) { s->hostname[p] = host[p]; p++; } s->hostname[p] = '\0';

    send_tcp(s->remote_ip, s->local_port, s->remote_port, s->seq, 0, 0x02, NULL, 0);

    for (int wait = 0; wait < 300; wait++) {
        net_poll();
        if (s->state == SOCK_ESTABLISHED || s->state == SOCK_TLS_READY) return slot;
        task_sleep(1);
    }
    s->state = SOCK_FREE;
    return -1;
}

int net_socket_send(int sock_id, const void *data, int len) {
    if (sock_id < 0 || sock_id >= MAX_SOCKETS || !data || len <= 0) return -1;
    NetSocket *s = &g_sockets[sock_id];
    if (s->state != SOCK_ESTABLISHED && s->state != SOCK_TLS_READY) return -1;

    send_tcp(s->remote_ip, s->local_port, s->remote_port, s->seq, s->ack, 0x18, data, (UINT16)len);
    s->seq += len;
    return len;
}

int net_socket_recv(int sock_id, void *buf, int max_len) {
    if (sock_id < 0 || sock_id >= MAX_SOCKETS || !buf || max_len <= 0) return 0;
    NetSocket *s = &g_sockets[sock_id];

    net_poll();
    if (s->rx_len > s->rx_read_pos) {
        UINT32 avail = s->rx_len - s->rx_read_pos;
        UINT32 chunk = avail < (UINT32)max_len ? avail : (UINT32)max_len;
        __builtin_memcpy(buf, &s->rx_buffer[s->rx_read_pos], chunk);
        s->rx_read_pos += chunk;
        if (s->rx_read_pos >= s->rx_len) { s->rx_len = 0; s->rx_read_pos = 0; }
        return (int)chunk;
    }
    return 0;
}

void net_socket_close(int sock_id) {
    if (sock_id < 0 || sock_id >= MAX_SOCKETS) return;
    NetSocket *s = &g_sockets[sock_id];
    if (s->state != SOCK_FREE) {
        send_tcp(s->remote_ip, s->local_port, s->remote_port, s->seq, s->ack, 0x11, NULL, 0);
        s->state = SOCK_FREE;
    }
}

int net_http_get(const char *url, char *out_buf, int max_len) {
    if (!url || !out_buf || max_len <= 0) return -1;

    int is_https = (url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p' && url[4] == 's');
    const char *host_start = is_https ? url + 8 : url + 7;

    char host[64];
    char path[128];
    const char *slash = host_start;
    while (*slash && *slash != '/') slash++;

    int hlen = (int)(slash - host_start);
    if (hlen > 63) hlen = 63;
    __builtin_memcpy(host, host_start, hlen);
    host[hlen] = '\0';

    if (*slash) {
        int plen = 0;
        while (slash[plen] && plen < 127) { path[plen] = slash[plen]; plen++; }
        path[plen] = '\0';
    } else {
        path[0] = '/'; path[1] = '\0';
    }

    int sock = net_socket_open(host, is_https ? 443 : 80, is_https);
    if (sock < 0) return -1;

    char req[512];
    int req_len = 0;
    const char *p1 = "GET "; while (*p1) req[req_len++] = *p1++;
    const char *p2 = path; while (*p2) req[req_len++] = *p2++;
    const char *p3 = " HTTP/1.1\r\nHost: "; while (*p3) req[req_len++] = *p3++;
    const char *p4 = host; while (*p4) req[req_len++] = *p4++;
    const char *p5 = "\r\nUser-Agent: VeloOS/1.0\r\nConnection: close\r\n\r\n"; while (*p5) req[req_len++] = *p5++;

    net_socket_send(sock, req, req_len);

    int total_read = 0;
    for (int wait = 0; wait < 400; wait++) {
        int r = net_socket_recv(sock, out_buf + total_read, max_len - 1 - total_read);
        if (r > 0) {
            total_read += r;
            wait = 0;
        }
        if (total_read > 0 && r == 0 && g_sockets[sock].state == SOCK_FREE) break;
        task_sleep(1);
    }
    out_buf[total_read] = '\0';
    net_socket_close(sock);
    return total_read;
}

// ==========================================
// E1000 PACKET POLLING & RECEIVE HANDLER
// ==========================================
void net_poll(void) {
    if (!g_mmio_base) return;

    while (g_rx_descs[g_rx_cur].status & 0x01) {
        UINT8 *buf = g_rx_buffers[g_rx_cur];
        UINT16 len = g_rx_descs[g_rx_cur].length;

        // 1. DHCP Response (UDP 68)
        if (len >= 42 && buf[12] == 0x08 && buf[13] == 0x00 && buf[23] == 17) {
            UINT16 src_p = swap16(*(UINT16*)&buf[34]);
            if (src_p == 67 && !g_net.dhcp_done) {
                g_net.ip = swap32(*(UINT32*)&buf[58]);
                g_net.gateway = 0x0A000202;
                g_net.dns = 0x0A000203;
                g_net.dhcp_done = 1;
                klog("[+] DHCP Lease erhalten. Online!\n");
            }
            else if (src_p == 53 && len >= 54) {
                UINT16 answers = swap16(*(UINT16*)&buf[48]);
                if (answers > 0) {
                    UINT32 ans_ip = swap32(*(UINT32*)&buf[len - 4]);
                    if (ans_ip != 0) {
                        g_dns_resolved_ip = ans_ip;
                        g_dns_resolved = 1;
                    }
                }
            }
        }
        // 2. TCP Packets
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
                    if ((flags & 0x12) == 0x12 && sock->state == SOCK_CONNECTING) {
                        sock->seq = ack_in;
                        sock->ack = seq_in + 1;
                        sock->state = sock->is_tls ? SOCK_TLS_READY : SOCK_ESTABLISHED;
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
                        sock->state = SOCK_FREE;
                    }
                }
            }
        }

        g_rx_descs[g_rx_cur].status = 0;
        UINT32 old_cur = g_rx_cur;
        g_rx_cur = (g_rx_cur + 1) % RX_NUM_DESC;
        mmio_write(E1000_REG_RDT, old_cur);
    }
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

void net_init(void) {
    g_mmio_base = 0;
    __builtin_memset(&g_net, 0, sizeof(NetworkState));
    __builtin_memset(g_sockets, 0, sizeof(g_sockets));
    g_net.tz_offset_hours = 1;
    __builtin_memcpy(g_net.timezone_abbr, "CET", 4);

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
    klog("[+] Intel Gigabit E1000 Netzwerkkarte online. DHCP gestartet.\n");
}

NetworkState* net_get_state(void) {
    return &g_net;
}
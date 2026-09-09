#ifndef NET_H
#define NET_H

#include <efi.h>
#include <efilib.h>

#define MAX_SOCKETS 8

#define SOCK_FREE        0
#define SOCK_CONNECTING  1
#define SOCK_ESTABLISHED 2
#define SOCK_CLOSING     3

#define HTTP_STATUS_IDLE    0
#define HTTP_STATUS_PENDING 1
#define HTTP_STATUS_READY   2
#define HTTP_STATUS_ERROR   3

#define DNS_GOOGLE_PRIMARY   0x08080808U // 8.8.8.8
#define DNS_CLOUDFLARE_SEC   0x01010101U // 1.1.1.1

typedef struct {
    UINT8  mac[6];
    UINT32 ip;
    UINT32 gateway;
    UINT32 subnet;
    UINT8  gateway_mac[6];
    int    gateway_mac_resolved;
    UINT32 dns_primary;
    UINT32 dns_secondary;
    int    link_up;
    int    dhcp_done;
    
    char   timezone_abbr[8];
    int    tz_offset_hours;
} NetworkState;

typedef struct {
    int    id;
    int    state;
    int    is_tls;
    UINT32 remote_ip;
    UINT16 local_port;
    UINT16 remote_port;
    UINT32 seq;
    UINT32 ack;
    UINT8  rx_buffer[65536]; // 64 KB Socket Puffer
    UINT32 rx_len;
    UINT32 rx_read_pos;
    char   hostname[64];
} NetSocket;

typedef struct {
    int    status;
    char   url[256];
    char   host[64];
    char   path[128];
    UINT16 port;
    int    sock_id;
    int    state_step;
    UINT32 ip;
    UINT8  buffer[131072]; // 128 KB Download Puffer
    int    bytes_read;
    int    timeout_ticks;
} AsyncHttpJob;

void net_init(void);
void net_poll(void);
NetworkState* net_get_state(void);

UINT32 net_dns_resolve(const char *hostname);
int    net_socket_open(const char *host, UINT16 port, int use_tls);
int    net_socket_send(int sock_id, const void *data, int len);
int    net_socket_recv(int sock_id, void *buf, int max_len);
void   net_socket_close(int sock_id);
int    net_http_get(const char *url, char *out_buf, int max_len);

int    net_http_async_start(const char *url);
int    net_http_async_poll(char *out_buf, int max_len, int *out_status, int *out_bytes);

#endif
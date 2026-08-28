#ifndef NET_H
#define NET_H

#include <efi.h>
#include <efilib.h>

#define MAX_SOCKETS 8

#define SOCK_FREE        0
#define SOCK_CONNECTING  1
#define SOCK_ESTABLISHED 2
#define SOCK_TLS_HANDSHAKE 3
#define SOCK_TLS_READY   4
#define SOCK_CLOSING     5

typedef struct {
    UINT8 mac[6];
    UINT32 ip;
    UINT32 gateway;
    UINT32 subnet;
    UINT32 dns;
    int link_up;
    int dhcp_done;
    int http_synced;
    
    char city[32];
    char country_code[8];
    char timezone_id[32];
    char timezone_abbr[8];
    int tz_offset_hours;
    
    int hour;
    int min;
    int sec;
} NetworkState;

typedef struct {
    int id;
    int state;
    int is_tls;
    UINT32 remote_ip;
    UINT16 local_port;
    UINT16 remote_port;
    UINT32 seq;
    UINT32 ack;
    UINT8  rx_buffer[8192];
    UINT32 rx_len;
    UINT32 rx_read_pos;
    
    // TLS 1.2 Session Context
    UINT8 client_random[32];
    UINT8 server_random[32];
    UINT8 master_secret[48];
    UINT8 client_write_key[16];
    UINT8 server_write_key[16];
    UINT8 client_write_iv[16];
    UINT8 server_write_iv[16];
    UINT8 client_write_mac[32];
    UINT8 server_write_mac[32];
    UINT64 client_seq_num;
    UINT64 server_seq_num;
    char hostname[64];
} NetSocket;

void net_init(void);
void net_poll(void);
NetworkState* net_get_state(void);

UINT32 net_dns_resolve(const char *hostname);
int    net_socket_open(const char *host, UINT16 port, int use_tls);
int    net_socket_send(int sock_id, const void *data, int len);
int    net_socket_recv(int sock_id, void *buf, int max_len);
void   net_socket_close(int sock_id);
int    net_http_get(const char *url, char *out_buf, int max_len);

#endif
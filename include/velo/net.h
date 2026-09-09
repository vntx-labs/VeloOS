#ifndef _VELO_NET_H
#define _VELO_NET_H

#include <velo/syscall.h>

typedef int velo_socket_t;

static inline UINT32 velo_dns_lookup(const char *hostname) {
    return (UINT32)velo_syscall(SYS_DNS_RESOLVE, (UINT64)hostname, 0, 0, 0);
}

static inline velo_socket_t velo_socket_open(const char *host, UINT16 port, int use_tls) {
    return (velo_socket_t)velo_syscall(SYS_SOCKET_OPEN, (UINT64)host, (UINT64)port, (UINT64)use_tls, 0);
}

static inline int velo_socket_write(velo_socket_t sock, const void *data, int len) {
    return (int)velo_syscall(SYS_SOCKET_SEND, (UINT64)sock, (UINT64)data, (UINT64)len, 0);
}

static inline int velo_socket_read(velo_socket_t sock, void *buf, int max_len) {
    return (int)velo_syscall(SYS_SOCKET_RECV, (UINT64)sock, (UINT64)buf, (UINT64)max_len, 0);
}

static inline void velo_socket_close(velo_socket_t sock) {
    velo_syscall(SYS_SOCKET_CLOSE, (UINT64)sock, 0, 0, 0);
}

static inline int velo_http_get(const char *url, char *out_buf, int max_len) {
    return (int)velo_syscall(SYS_HTTP_GET, (UINT64)url, (UINT64)out_buf, (UINT64)max_len, 0);
}

static inline int velo_https_get(const char *url, char *out_buf, int max_len) {
    return velo_http_get(url, out_buf, max_len);
}

static inline void velo_thread_sleep(UINT32 ticks) {
    velo_syscall(SYS_TASK_SLEEP, (UINT64)ticks, 0, 0, 0);
}

static inline void velo_thread_yield(void) {
    velo_syscall(SYS_TASK_YIELD, 0, 0, 0, 0);
}

#endif
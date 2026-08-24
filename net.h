#ifndef NET_H
#define NET_H

#include <efi.h>
#include <efilib.h>

typedef struct {
    UINT8 mac[6];
    UINT32 ip;
    UINT32 gateway;
    UINT32 subnet;
    UINT32 dns;
    int link_up;
    int dhcp_done;
    int http_synced;
    
    // Standort- & Zeitzonendaten
    char city[32];
    char country_code[8];
    char timezone_id[32];      // z. B. "Europe/Berlin"
    char timezone_abbr[8];    // z. B. "CET" oder "CEST"
    int tz_offset_hours;       // z. B. +1 oder +2
    
    // Live-Uhrzeit
    int hour;
    int min;
    int sec;
} NetworkState;

void net_init(void);
void net_poll(void);
NetworkState* net_get_state(void);
void net_fetch_location_and_time(void);

#endif
/**
 * lwIP configuration for Pico Synth WiFi server
 */

#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// Common settings for pico_cyw43_arch
#define NO_SYS                          1
#define LWIP_SOCKET                     0
#define LWIP_NETCONN                    0

// Memory settings
#define MEM_LIBC_MALLOC                 0
#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        8000
#define MEMP_NUM_TCP_SEG                32
#define MEMP_NUM_ARP_QUEUE              10
#define MEMP_NUM_TCP_PCB                5
#define MEMP_NUM_TCP_PCB_LISTEN         2
#define PBUF_POOL_SIZE                  24
#define LWIP_ARP                        1
#define LWIP_ETHERNET                   1
#define LWIP_ICMP                       1
#define LWIP_RAW                        1
#define LWIP_NETIF_STATUS_CALLBACK      1

// TCP settings
#define LWIP_TCP                        1
#define TCP_MSS                         1460
#define TCP_WND                         (8 * TCP_MSS)
#define TCP_SND_BUF                     (8 * TCP_MSS)
#define TCP_SND_QUEUELEN                ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))

// UDP (needed for DHCP)
#define LWIP_UDP                        1
#define LWIP_DHCP                       1
#define DHCP_DOES_ARP_CHECK             0
#define LWIP_IGMP                       0
#define LWIP_DNS                        0

// DHCP Server (for AP mode)
#define MEMP_NUM_UDP_PCB                4

// IP settings
#define LWIP_IPV4                       1
#define LWIP_IPV6                       0

// Checksum settings (use hardware where possible)
#define LWIP_CHKSUM_ALGORITHM           3

// HTTP server needs these
#define LWIP_HTTPD                      0
#define LWIP_CALLBACK_API               1

// Debugging (disable for production)
#define LWIP_DEBUG                      0

// Stats (disable to save memory)
#define LWIP_STATS                      0
#define LWIP_STATS_DISPLAY              0

#endif // _LWIPOPTS_H

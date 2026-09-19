#ifndef VIRTIO_H
#define VIRTIO_H


#include <types.h>
#include <process.h>


#define MAX_SOCKETS 16

//HEADERS DOS PACOTES

//O virtio tem um header especial de 10 bytes antes dos pacotes
struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __attribute__((packed));

struct eth_hdr {
    uint8_t dest_mac[6];
    uint8_t src_mac[6];
    uint16_t ethertype;
} __attribute__((packed));

struct ipv4_hdr {
    uint8_t ihl_version;
    uint8_t tos;
    uint16_t tot_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t ttl;
    uint8_t protocol; // UDP = 17
    uint16_t check;
    uint32_t saddr;
    uint32_t daddr;
} __attribute__((packed));

struct udp_hdr {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t len;
    uint16_t check;
} __attribute__((packed));


struct arp_hdr {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t opcode;
    uint8_t  sender_mac[6];
    uint8_t  sender_ip[4];
    uint8_t  target_mac[6];
    uint8_t  target_ip[4];
} __attribute__((packed));


//Um socket
struct socket {
    uint32_t em_uso;
    uint16_t porta_local;
    uint8_t rx_buffer[2048];
    uint32_t rx_tamanho;
    struct process *processo_bloqueado;
};




extern struct process *current;


// Funções de rede
int16_t virtio_net_init(void);
int32_t sys_socket(uint16_t porta);
uint32_t sys_recv(int32_t sock_id, void *buffer_user, uint32_t tam_max);
int32_t sys_send(int32_t sock_id, void *buffer_user, uint32_t len);

#define ntohs(x) __builtin_bswap16(x)

#endif
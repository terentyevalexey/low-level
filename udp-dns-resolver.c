#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

typedef struct {
    uint16_t id;
    uint8_t params_1;
    uint8_t params_2;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((__packed__)) dns_header_t;

typedef struct {
    uint16_t qtype;
    uint16_t qclass;
} __attribute__((__packed__)) dns_query_tail_t;

typedef struct {
    uint16_t name;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t rdlength;
} __attribute__((__packed__)) dns_answer_t;

int main(int argc, char* argv[]) {
    // setting socket
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == -1) {
        perror("socket");
        exit(1);
    }

    // binding socket to an address
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("8.8.8.8");
    addr.sin_port = htons(53);

    unsigned char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    unsigned char* qname = &buffer[sizeof(dns_header_t)];
    uint16_t id = getpid();

    while (scanf("%s", &qname[1]) != EOF) {
        ++id;
        // setting header and tail
        dns_header_t header;
        memset(&header, 0, sizeof(header));
        header.id = id;
        header.params_1 = 1;
        header.qdcount = htons(1);
        // others equal to 0
        dns_query_tail_t tail;
        tail.qtype = htons(1);
        tail.qclass = htons(1);

        // creating request
        memcpy(buffer, &header, sizeof(header));
        unsigned char* prev_pos = qname;
        unsigned char* cur_pos = qname;
        *qname = 1; // for strchr non-zero
        while (cur_pos = strchr(prev_pos + 1, '.')) {
            *prev_pos = cur_pos - prev_pos - 1;
            prev_pos = cur_pos;
        }
        int size = strlen(qname);
        *prev_pos = qname - prev_pos + size - 1;
        size += sizeof(header) + 1;
        memcpy(buffer + size, &tail, sizeof(tail));
        size += sizeof(tail);

        // sending
        if (sendto(sock,
                   &buffer,
                   size,
                   0,
                   (struct sockaddr*)&addr,
                   sizeof(addr)
                  ) < 0) {
            perror("send");
            exit(1);
        }
        // receiving
        do {
            memset(buffer, 0, sizeof(buffer));
            int addr_size = sizeof(addr);
            if (recvfrom(sock,
                         buffer,
                         sizeof(buffer),
                         0,
                         (struct sockaddr*)&addr,
                         &addr_size
                ) < 0) {
                perror("recv");
                exit(1);
            }
        } while (*(uint16_t*)buffer != id);

        int offset = size;
        dns_answer_t* answer = (dns_answer_t*)&buffer[offset];
        while (answer->type != htons(1) || answer->class != htons(1)) {
            offset += ntohs(answer->rdlength) + sizeof(dns_answer_t);
            answer = (dns_answer_t*)&buffer[offset];
        }

        offset += sizeof(dns_answer_t);

        printf("%d.%d.%d.%d\n",
               buffer[offset],
               buffer[offset+1],
               buffer[offset+2],
               buffer[offset+3]);
        memset(buffer, 0, sizeof(buffer));
    }
    close(sock);
}

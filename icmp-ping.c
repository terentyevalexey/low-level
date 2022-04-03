#include <unistd.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netdb.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <linux/filter.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>

int16_t get_checksum(struct icmp* icmp_header) {
    int64_t sum = (icmp_header->icmp_type << 8) + icmp_header->icmp_code +
                  ntohs(icmp_header->icmp_cksum) +
                  ntohs(icmp_header->icmp_id) +
                  ntohs(icmp_header->icmp_seq);
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return ~sum & 0xffff;
}

int set_socket(uint32_t id) {
    int sock = socket(AF_INET, SOCK_RAW | SOCK_NONBLOCK, IPPROTO_ICMP);
    if (sock == -1) {
        perror("socket");
        exit(1);
    }
    // filtering only ip packages that have icmp echo replies with needed id
    struct sock_filter code[] = {
        {0x30, 0, 0, 0x00000000 },
        {0x15, 0, 5, 0x00000045 },
        {0x28, 0, 0, 0x00000014 },
        {0x15, 0, 3, 0x00000000 },
        {0x28, 0, 0, 0x00000018 },
        {0x15, 0, 1, id & 0x0000ffff},
        {0x06, 0, 0, 0xffffffff },
        {0x06, 0, 0, 0x00000000 },
    };

    struct sock_fprog program = {
        .filter = code,
        .len = sizeof(code) / sizeof(code[0])
    };
    setsockopt(sock,
               SOL_SOCKET,
               SO_ATTACH_FILTER,
               &program,
               sizeof(program));
    return sock;
}

void set_dest_addr(struct sockaddr_in* dest_addr, const char* host_name) {
    struct hostent* dst_host;
    if ((dst_host = gethostbyname(host_name)) == NULL) {
        printf("0\n");
        exit(0);
    }
    dest_addr->sin_addr = *(struct in_addr*)dst_host->h_addr;
    dest_addr->sin_family = AF_INET;
}

void set_icmp_echo_request_header(struct icmp* icmp_header, uint32_t id) {
    icmp_header->icmp_type = 8;  // echo request
    icmp_header->icmp_code = 0;  // 0 for echo request
    icmp_header->icmp_cksum = htons(0);  // to recalculate later
    icmp_header->icmp_id = htons(id);  // id
    icmp_header->icmp_seq = htons(0);  // cur package
}

int main(int argc, char* argv[]) {
    struct timeval start_time;
    gettimeofday(&start_time, NULL);

    const char* host_name = argv[1];
    int timeout = atoi(argv[2]);

    long int interval = atol(argv[3]);

    uint32_t id = getpid();

    int sock = set_socket(id);

    // setting the icmp echo request header block
    struct icmp icmp_header;
    memset(&icmp_header, 0, sizeof(icmp_header));
    set_icmp_echo_request_header(&icmp_header, id);

    // setting destination address
    struct sockaddr_in dest_addr;
    set_dest_addr(&dest_addr, host_name);
    int dest_addr_len = sizeof(dest_addr);

    uint16_t cur_package = 0;

    // preparing space for answer
    char answer_buf[576];  // maximum icmp answer length
    memset(answer_buf, 0, sizeof(answer_buf));
    struct ip* ip_answer_block = (struct ip*)answer_buf;
    struct icmp* icmp_answer_block = (struct icmp*)(ip_answer_block+1);

    int succeeded_packages = 0;

    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    struct timeval previous_time = current_time;
    while ( current_time.tv_sec - start_time.tv_sec < timeout ||
           (current_time.tv_sec - start_time.tv_sec == timeout &&
            current_time.tv_usec < start_time.tv_usec)) {
        gettimeofday(&previous_time, NULL);
        icmp_header.icmp_seq = htons(cur_package);
        icmp_header.icmp_cksum = htons(0);
        icmp_header.icmp_cksum = htons(get_checksum(&icmp_header));
        // sending
        sendto(sock,
               &icmp_header,
               sizeof(struct icmp),
               0,
               (struct sockaddr*)&dest_addr,
               sizeof(dest_addr)
              );
        usleep(interval);
        while (recv(sock,
                    answer_buf,
                    sizeof(answer_buf),
                    0
                    ) > 0 && get_checksum(icmp_answer_block) == 0) {
            ++succeeded_packages;
        }
        ++cur_package;
        gettimeofday(&current_time, NULL);
    }
    printf("%d\n", succeeded_packages);
    close(sock);
}

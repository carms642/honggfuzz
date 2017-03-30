#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sched.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

void HF_ITER(uint8_t**, size_t*);

void fatal(const char* fmt, ...)
{
    fprintf(stdout, "[-] ");

    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);

    fprintf(stdout, "\n");

    exit(1);
}

void pfatal(const char* fmt, ...)
{
    fprintf(stdout, "[-] ");

    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);

    fprintf(stdout, ": %s\n", strerror(errno));

    exit(1);
}

void mlog(const char* fmt, ...)
{
    fprintf(stdout, "[+] ");

    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);

    fprintf(stdout, "\n");
}

int main(void)
{
    if (unshare(CLONE_NEWUSER | CLONE_NEWNET) == -1) {
        pfatal("unshare()");
    }

    struct ifreq ifr;
    memset(&ifr, '\0', sizeof(ifr));
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI | IFF_NOFILTER;
    strcpy(ifr.ifr_name, "FUZZ0");

    int fd = open("/dev/net/tun", O_RDWR);
    if (fd == -1) {
        pfatal("open('/dev/net/tun')");
    }
    if (ioctl(fd, TUNSETIFF, (void*)&ifr) != 0) {
        pfatal("ioctl(TUNSETIFF)");
    }
    if (ioctl(fd, TUNSETNOCSUM, 1) != 0) {
        pfatal("ioctl(TUNSETNOCSUM)");
    }

    int udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (udp_sock == -1) {
        pfatal("socket(AF_INET, SOCK_DGRAM, IPPROTO_IP)");
    }
    int tcp_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (tcp_sock == -1) {
        pfatal("socket(AF_INET, SOCK_STREAM, IPPROTO_IP)");
    }

    struct sockaddr_in* sa = (struct sockaddr_in*)(&ifr.ifr_addr);
    sa->sin_family = AF_INET;
    sa->sin_addr.s_addr = inet_addr("192.168.255.1");
    if (ioctl(tcp_sock, SIOCSIFADDR, &ifr) == -1) {
        pfatal("ioctl(tcp_sock, SIOCSIFADDR, &ifr)");
    }
    sa->sin_addr.s_addr = inet_addr("192.168.255.2");
    if (ioctl(tcp_sock, SIOCSIFDSTADDR, &ifr) == -1) {
        pfatal("ioctl(tcp_sock, SIOCSIFDSTADDR, &ifr)");
    }

    if (ioctl(tcp_sock, SIOCGIFFLAGS, &ifr) == -1) {
        pfatal("ioctl(tcp_sock, SIOCGIFFLAGS, &ifr)");
    }
    ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);
    if (ioctl(tcp_sock, SIOCSIFFLAGS, &ifr) == -1) {
        pfatal("ioctl(tcp_sock, SIOCSIFFLAGS, &ifr)");
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(1337),
        .sin_addr.s_addr = INADDR_ANY,
    };

    if (bind(tcp_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        pfatal("bind(tcp)");
    }
    if (bind(udp_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        pfatal("bind(udp)");
    }
    if (fcntl(fd, F_SETFL, O_NONBLOCK | O_RDWR) == -1) {
        pfatal("fcntl(fd, F_SETFL, O_NONBLOCK|O_RDWR)");
    }
    if (fcntl(tcp_sock, F_SETFL, O_NONBLOCK | O_RDWR) == -1) {
        pfatal("fcntl(tcp_sock, F_SETFL, O_NONBLOCK|O_RDWR)");
    }
    if (fcntl(udp_sock, F_SETFL, O_NONBLOCK | O_RDWR) == -1) {
        pfatal("fcntl(sock, F_SETFL, O_NONBLOCK|O_RDWR)");
    }

    if (listen(tcp_sock, SOMAXCONN) == -1) {
        pfatal("listen(tcp_sock)");
    }

    int tcp_acc_sock = -1;

    for (;;) {
        uint8_t* buf;
        size_t len;

        HF_ITER(&buf, &len);

        while (len > 0) {
            size_t tlen = (len > 1500) ? 1500 : len;
            write(fd, buf, tlen);
            len -= tlen;
        }

        for (;;) {
            char b[1024 * 128];
            if (read(fd, b, sizeof(b)) <= 0) {
                break;
            }
        }

        if (tcp_acc_sock == -1) {
            struct sockaddr_in nsock;
            socklen_t slen = sizeof(nsock);
            tcp_acc_sock = accept4(tcp_sock, (struct sockaddr*)&nsock, &slen, SOCK_NONBLOCK);
        }
        if (tcp_acc_sock != -1) {
            char b[1024 * 128];
            if (recv(tcp_acc_sock, b, sizeof(b), MSG_DONTWAIT) == 0) {
                close(tcp_acc_sock);
                tcp_acc_sock = -1;
            }
            send(tcp_acc_sock, b, 1, MSG_NOSIGNAL | MSG_DONTWAIT);
        }

        char b[1024 * 128];

        struct sockaddr_in addr;
        socklen_t slen = sizeof(addr);
        if (recvfrom(udp_sock, b, sizeof(b), MSG_DONTWAIT, (struct sockaddr*)&addr, &slen) > 0) {
            sendto(udp_sock, b, 1, MSG_NOSIGNAL | MSG_DONTWAIT, (struct sockaddr*)&addr, slen);
        }
    }
}
#include "TAP.h"

char tap_name[IFNAMSIZ];

extern bool verbose;
extern bool noipv6;
extern bool set_ipv4;
extern bool set_netmask;
extern bool noup;
extern int mtu;
extern int device_type;
extern char if_name[IFNAMSIZ];
extern char* ipv4_addr;
extern char* netmask;
extern void cleanup();

int open_tap(void) {
    struct ifreq ifr;
    int fd = open("/dev/net/tun", O_RDWR);

    if (fd < 0) {
        perror("Could not open clone device");
        exit(1);
    } else {
        memset(&ifr, 0, sizeof(ifr));
        // TODO: Enable PI header again?

        if (device_type == IF_TAP) {
            ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
        } else if (device_type == IF_TUN) {
            ifr.ifr_flags = IFF_TUN;
        } else {
            printf("Error: Unsupported interface type\r\n");
            cleanup();
            exit(1);
        }

        strcpy(tap_name, "tnc%d");
        strncpy(ifr.ifr_name, tap_name, IFNAMSIZ);

        if (ioctl(fd, TUNSETIFF, &ifr) < 0) {
            perror("Could not configure network interface");
            exit(1);
        } else {
            strcpy(if_name, ifr.ifr_name);
            
            int inet = socket(AF_INET, SOCK_DGRAM, 0);
            if (inet == -1) {
                perror("Could not open AF_INET socket");
                cleanup();
                exit(1);
            } else {
                if (ioctl(inet, SIOCGIFMTU, &ifr) < 0) {
                    perror("Could not get interface flags from kernel");
                    close(inet);
                    cleanup();
                    exit(1);
                } else {
                    ifr.ifr_mtu = mtu;
                    if (ioctl(inet, SIOCSIFMTU, &ifr) < 0) {
                        perror("Could not configure interface MTU");
                        close(inet);
                        cleanup();
                        exit(1);
                    }

                    // Configure TX queue length
                    if (ioctl(inet, SIOCGIFTXQLEN, &ifr) < 0) {
                        perror("Could not get interface flags from kernel");
                        close(inet);
                        cleanup();
                        exit(1);
                    } else {
                        ifr.ifr_qlen = TXQUEUELEN;
                        if (ioctl(inet, SIOCSIFTXQLEN, &ifr) < 0) {
                            perror("Could not set interface TX queue length");
                            close(inet);
                            cleanup();
                            exit(1);
                        }
                    }

                    // Configure ARP characteristics
                    char path_buf[256];
                    if (device_type == IF_TAP) {
                        snprintf(path_buf, sizeof(path_buf), "/proc/sys/net/ipv4/neigh/%s/base_reachable_time_ms", ifr.ifr_name);
                        int arp_fd = open(path_buf, O_WRONLY);
                        if (arp_fd < 0) {
                            perror("Could not open proc entry for ARP parameters");
                            close(inet);
                            cleanup();
                            exit(1);
                        } else {
                            if (dprintf(arp_fd, "%d", ARP_BASE_REACHABLE_TIME*1000) <= 0) {
                                perror("Could not configure interface ARP parameter base_reachable_time_ms");
                                close(inet);
                                close(arp_fd);
                                cleanup();
                                exit(1);
                            } else {
                                close(arp_fd);
                            }
                        }

                        snprintf(path_buf, sizeof(path_buf), "/proc/sys/net/ipv4/neigh/%s/retrans_time_ms", ifr.ifr_name);
                        arp_fd = open(path_buf, O_WRONLY);
                        if (arp_fd < 0) {
                            perror("Could not open proc entry for ARP parameters");
                            close(inet);
                            cleanup();
                            exit(1);
                        } else {
                            if (dprintf(arp_fd, "%d", ARP_RETRANS_TIME*1000) <= 0) {
                                perror("Could not configure interface ARP parameter retrans_time_ms");
                                close(inet);
                                close(arp_fd);
                                cleanup();
                                exit(1);
                            } else {
                                close(arp_fd);
                            }
                        }
                    }

                    // Bring up if requested
                    if (!noup) {
                        if (ioctl(inet, SIOCGIFFLAGS, &ifr) < 0) {
                            perror("Could not get interface flags from kernel");
                            close(inet);
                            cleanup();
                            exit(1);
                        } else {
                            ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
                            if (ioctl(inet, SIOCSIFFLAGS, &ifr) < 0) {
                                perror("Could not bring up interface");
                                close(inet);
                                cleanup();
                                exit(1);
                            } else {
                                if (set_ipv4) {
                                    struct ifreq a_ifr;
                                    struct sockaddr_in addr, snm;

                                    memset(&a_ifr, 0, sizeof(a_ifr));
                                    memset(&addr, 0, sizeof(addr));
                                    memset(&snm, 0, sizeof(addr));
                                    strncpy(a_ifr.ifr_name, ifr.ifr_name, IFNAMSIZ);
                                    addr.sin_family = AF_INET;
                                    snm.sin_family = AF_INET;

                                    int addr_conversion = inet_pton(AF_INET, ipv4_addr, &(addr.sin_addr));
                                    if (addr_conversion != 1) {
                                        printf("Error: Invalid IPv4 address specified\r\n");
                                        close(inet);
                                        cleanup();
                                        exit(1);
                                    } else {
                                        a_ifr.ifr_addr = *(struct sockaddr*)&addr;
                                        if (ioctl(inet, SIOCSIFADDR, &a_ifr) < 0) {
                                            perror("Could not set IP-address");
                                            close(inet);
                                            cleanup();
                                            exit(1);
                                        } else {
                                            if (set_netmask) {
                                                int snm_conversion = inet_pton(AF_INET, netmask, &(snm.sin_addr));
                                                if (snm_conversion != 1) {
                                                    printf("Error: Invalid subnet mask specified\r\n");
                                                    close(inet);
                                                    cleanup();
                                                    exit(1);
                                                } else {
                                                    a_ifr.ifr_addr = *(struct sockaddr*)&snm;
                                                    if (ioctl(inet, SIOCSIFNETMASK, &a_ifr) < 0) {
                                                        perror("Could not set subnet mask");
                                                        close(inet);
                                                        cleanup();
                                                        exit(1);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            return fd;
        }
    }
}

int close_tap(int tap_fd) {
    return close(tap_fd);
}
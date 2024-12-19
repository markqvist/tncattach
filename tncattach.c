#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <poll.h>
#include <argp.h>
#include <syslog.h>
#include <sys/stat.h>
#include <time.h>
#include "Constants.h"
#include "Serial.h"
#include "KISS.h"
#include "TCP.h"
#include "TAP.h"

#define BAUDRATE_DEFAULT 0
#define SERIAL_BUFFER_SIZE 512

#define IF_FD_INDEX 0
#define TNC_FD_INDEX 1
#define N_FDS 2

struct pollfd fds[N_FDS];

int attached_tnc;
int attached_if;

char if_name[IFNAMSIZ];

uint8_t serial_buffer[MTU_MAX];
uint8_t if_buffer[MTU_MAX];

bool verbose = false;
bool noipv6 = false;
bool noup = false;
bool daemonize = false;
bool set_ipv4 = false;
bool set_ipv6 = false;
bool set_linklocal = false;
bool set_netmask = false;
bool kiss_over_tcp = false;
char* ipv4_addr;
char* netmask;

char* ipv6_addr;
long ipv6_prefixLen;

char* tcp_host;
int tcp_port;

int mtu;
int device_type = IF_TUN;

char* id;
int id_interval = -1;
time_t last_id = 0;
bool tx_since_last_id = false;

void cleanup(void) {
    if (kiss_over_tcp) {
        close_tcp(attached_tnc);
    } else {
        close_port(attached_tnc);
    }
    close_tap(attached_if);
}

bool is_ipv6(uint8_t* frame) {
    if (device_type == IF_TAP) {
        if (frame[12] == 0x86 && frame[13] == 0xdd) {
            return true;
        } else {
            return false;
        }
    } else if (device_type == IF_TUN) {
        if (frame[2] == 0x86 && frame[3] == 0xdd) {
            return true;
        } else {
            return false;
        }
    } else {
        printf("Error: Unsupported interface type\r\n");
        cleanup();
        exit(1);
    }
}

time_t time_now(void) {
    time_t now = time(NULL);
    if (now == -1) {
        if (daemonize) {
            syslog(LOG_ERR, "Could not get system time, exiting now");
        } else {
            printf("Error: Could not get system time, exiting now\r\n");
        }
        cleanup();
        exit(1);
    } else {
        return now;
    }
}

void transmit_id(void) {
    time_t now = time(NULL);
    int id_len = strlen(id);
    if (verbose) {
        if (!daemonize) {
            printf("Transmitting %d bytes of identification data on %s: %s\r\n", id_len, if_name, id);
        }
    }

    uint8_t* id_frame = malloc(strlen(id));
    memcpy(id_frame, id, id_len);
    kiss_write_frame(attached_tnc, id_frame, id_len);
    last_id = now;
    tx_since_last_id = false;

}

bool should_id(void) {
    if (id_interval != -1) {
        time_t now = time_now();
        return now > last_id + id_interval;
    } else {
        return false;
    }
}

void signal_handler(int signal) {
    if (daemonize) syslog(LOG_NOTICE, "tncattach daemon exiting");

    // Transmit final ID if necessary
    if (id_interval != -1 && tx_since_last_id) transmit_id();

    cleanup();
    exit(0);
}

void read_loop(void) {
    bool should_continue = true;
    int min_frame_size;
    if (device_type == IF_TAP) {
        min_frame_size = ETHERNET_MIN_FRAME_SIZE;
    } else if (device_type == IF_TUN) {
        min_frame_size = TUN_MIN_FRAME_SIZE;
    } else {
        if (daemonize) {
            syslog(LOG_ERR, "Unsupported interface type");
        } else {
            printf("Error: Unsupported interface type\r\n");
        }

        cleanup();
        exit(1);
    }

    int poll_timeout = 1000;
    while (should_continue) {
        int poll_result = poll(fds, 2, poll_timeout);
        if (poll_result != -1) {
            if (poll_result == 0) {
                // No resources are ready for reading,
                // run scheduled tasks instead.
                if (id_interval != -1 && tx_since_last_id) {
                    time_t now = time_now();
                    if (now > last_id + id_interval) transmit_id();
                }
            } else {
                for (int fdi = 0; fdi < N_FDS; fdi++) {
                    if (fds[fdi].revents != 0) {
                        // Check for hangup event
                        if (fds[fdi].revents & POLLHUP) {
                            if (fdi == IF_FD_INDEX) {
                                if (daemonize) {
                                    syslog(LOG_ERR, "Received hangup from interface");
                                } else {
                                    printf("Received hangup from interface\r\n");
                                }
                                cleanup();
                                exit(1);
                            }
                            if (fdi == TNC_FD_INDEX) {
                                if (daemonize) {
                                    syslog(LOG_ERR, "Received hangup from TNC");
                                } else {
                                    printf("Received hangup from TNC\r\n");
                                }
                                cleanup();
                                exit(1);
                            }
                        }

                        // Check for error event
                        if (fds[fdi].revents & POLLERR) {
                            if (fdi == IF_FD_INDEX) {
                                if (daemonize) {
                                    syslog(LOG_ERR, "Received error event from interface");
                                } else {
                                    perror("Received error event from interface\r\n");
                                }
                                cleanup();
                                exit(1);
                            }
                            if (fdi == TNC_FD_INDEX) {
                                if (daemonize) {
                                    syslog(LOG_ERR, "Received error event from TNC");
                                } else {
                                    perror("Received error event from TNC\r\n");
                                }
                                cleanup();
                                exit(1);
                            }
                        }

                        // If data is ready, read it
                        if (fds[fdi].revents & POLLIN) {
                            if (fdi == IF_FD_INDEX) {
                                int if_len = read(attached_if, if_buffer, sizeof(if_buffer));
                                if (if_len > 0) {
                                    if (if_len >= min_frame_size) {
                                        if (!noipv6 || (noipv6 && !is_ipv6(if_buffer))) {

                                            int tnc_written = kiss_write_frame(attached_tnc, if_buffer, if_len);
                                            if (verbose && !daemonize) printf("Got %d bytes from interface, wrote %d bytes (KISS-framed and escaped) to TNC\r\n", if_len, tnc_written);
                                            tx_since_last_id = true;

                                            if (should_id()) transmit_id();
                                        }
                                    }
                                } else {
                                    if (daemonize) {
                                        syslog(LOG_ERR, "Could not read from network interface, exiting now");
                                    } else {
                                        printf("Error: Could not read from network interface, exiting now\r\n");
                                    }
                                    cleanup();
                                    exit(1);
                                }
                            }

                            if (fdi == TNC_FD_INDEX) {
                                int tnc_len = read(attached_tnc, serial_buffer, sizeof(serial_buffer));
                                if (tnc_len > 0) {
                                    for (int i = 0; i < tnc_len; i++) {
                                        kiss_serial_read(serial_buffer[i]);
                                    }
                                } else {
                                    if (daemonize) {
                                        syslog(LOG_ERR, "Could not read from TNC, exiting now");
                                    } else {
                                        printf("Error: Could not read from TNC, exiting now\r\n");
                                    }

                                    cleanup();
                                    exit(1);
                                }
                            }
                        }
                    }
                }
            }
        } else {
            should_continue = false;
        }
    }
    cleanup();
    exit(1);
}

const char *argp_program_version = "tncattach 0.1.9";
const char *argp_program_bug_address = "<mark@unsigned.io>";
static char doc[] = "\r\nAttach TNC devices as system network interfaces\vTo attach the TNC connected to /dev/ttyUSB0 as an ethernet device with an MTU of 512 bytes and assign an IPv4 address, while filtering IPv6 traffic, use:\r\n\r\n\ttncattach /dev/ttyUSB0 115200 -m 512 -e --noipv6 --ipv4 10.0.0.1/24\r\n\r\nStation identification can be performed automatically to comply with Part 97 rules. See the README for a complete description. Use the --id and --interval options, which should commonly be set to your callsign, and 600 seconds.";
static char args_doc[] = "port baudrate";
static struct argp_option options[] = {
    { "mtu", 'm', "MTU", 0, "Specify interface MTU", 1},
    { "ethernet", 'e', 0, 0, "Create a full ethernet device", 2},
    { "ipv4", 'i', "IP_ADDRESS", 0, "Configure an IPv4 address on interface", 3},
    { "ipv6", '6', "IP6_ADDRESS", 0, "Configure an IPv6 address on interface", 4},
    { "ll", 'l', 0, 0, "Add a link-local Ipv6 address", 5},
    { "noipv6", 'n', 0, 0, "Filter IPv6 traffic from reaching TNC", 6},
    { "noup", 1, 0, 0, "Only create interface, don't bring it up", 7},
    { "kisstcp", 'T', 0, 0, "Use KISS over TCP instead of serial port", 8},
    { "tcphost", 'H', "TCP_HOST", 0, "Host to connect to when using KISS over TCP", 9},
    { "tcpport", 'P', "TCP_PORT", 0, "TCP port when using KISS over TCP", 10},
    { "interval", 't', "SECONDS", 0, "Maximum interval between station identifications", 11},
    { "id", 's', "CALLSIGN", 0, "Station identification data", 12},
    { "daemon", 'd', 0, 0, "Run tncattach as a daemon", 13},
    { "verbose", 'v', 0, 0, "Enable verbose output", 14},
    { 0 }
};

#define N_ARGS 2
struct arguments {
    char *args[N_ARGS];
    char *ipv4;
    char *ipv6;
    char *id;
    bool valid_id;
    int id_interval;
    int baudrate;
    int tcpport;
    int mtu;
    bool tap;
    bool daemon;
    bool verbose;
    bool set_ipv4;
    bool set_netmask;
    bool set_ipv6;
    bool link_local_v6;
    bool set_netmask_v6;
    bool noipv6;
    bool noup;
    bool kiss_over_tcp;
    bool set_tcp_host;
    bool set_tcp_port;
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    struct arguments *arguments = state->input;

    switch (key) {
        case 'v':
            arguments->verbose = true;
            break;

        case 'e':
            arguments->tap = true;
            break;

        case 'm':
            arguments->mtu = atoi(arg);
            if (arguments->mtu < MTU_MIN || arguments->mtu > MTU_MAX) {
                printf("Error: Invalid MTU specified\r\n\r\n");
                argp_usage(state);
            }

            if((arguments->set_ipv6 || arguments->link_local_v6) && arguments->mtu < 1280)
            {
                printf("IPv6 and/or link-local IPv6 was requested, but the MTU provided is lower than 1280\n");
                exit(EXIT_FAILURE);
            }

            break;

        case 't':
            arguments->id_interval = atoi(arg);
            if (arguments->id_interval < 0) {
                printf("Error: Invalid identification interval specified\r\n\r\n");
                argp_usage(state);
            }
            break;

        case 's':
            arguments->id = arg;
            if (strlen(arg) < 1 || strlen(arg) > arguments->mtu) {
                printf("Error: Invalid identification string specified\r\n\r\n");
                argp_usage(state);
            } else {
                arguments->valid_id = true;
            }
            break;

        case 'i':
            arguments->ipv4 = arg;
            arguments->set_ipv4 = true;

            if (strchr(arg, '/')) {
                char* net = strchr(arg, '/');
                int pos = net-arg;
                ipv4_addr = (char*)malloc(pos+1);
                int mask = atoi(net+1);
                strncpy(ipv4_addr, arg, pos);
                switch (mask) {
                    case 0:
                        netmask = "0.0.0.0";
                        break;
                    case 1:
                        netmask = "128.0.0.0";
                        break;
                    case 2:
                        netmask = "192.0.0.0";
                        break;
                    case 3:
                        netmask = "224.0.0.0";
                        break;
                    case 4:
                        netmask = "240.0.0.0";
                        break;
                    case 5:
                        netmask = "248.0.0.0";
                        break;
                    case 6:
                        netmask = "252.0.0.0";
                        break;
                    case 7:
                        netmask = "254.0.0.0";
                        break;
                    case 8:
                        netmask = "255.0.0.0";
                        break;
                    case 9:
                        netmask = "255.128.0.0";
                        break;
                    case 10:
                        netmask = "255.192.0.0";
                        break;
                    case 11:
                        netmask = "255.224.0.0";
                        break;
                    case 12:
                        netmask = "255.240.0.0";
                        break;
                    case 13:
                        netmask = "255.248.0.0";
                        break;
                    case 14:
                        netmask = "255.252.0.0";
                        break;
                    case 15:
                        netmask = "255.254.0.0";
                        break;
                    case 16:
                        netmask = "255.255.0.0";
                        break;
                    case 17:
                        netmask = "255.255.128.0";
                        break;
                    case 18:
                        netmask = "255.255.192.0";
                        break;
                    case 19:
                        netmask = "255.255.224.0";
                        break;
                    case 20:
                        netmask = "255.255.240.0";
                        break;
                    case 21:
                        netmask = "255.255.248.0";
                        break;
                    case 22:
                        netmask = "255.255.252.0";
                        break;
                    case 23:
                        netmask = "255.255.254.0";
                        break;
                    case 24:
                        netmask = "255.255.255.0";
                        break;
                    case 25:
                        netmask = "255.255.255.128";
                        break;
                    case 26:
                        netmask = "255.255.255.192";
                        break;
                    case 27:
                        netmask = "255.255.255.224";
                        break;
                    case 28:
                        netmask = "255.255.255.240";
                        break;
                    case 29:
                        netmask = "255.255.255.248";
                        break;
                    case 30:
                        netmask = "255.255.255.252";
                        break;
                    case 31:
                        netmask = "255.255.255.254";
                        break;
                    case 32:
                        netmask = "255.255.255.255";
                        break;

                    default:
                        printf("Error: Invalid subnet mask specified\r\n");
                        cleanup();
                        exit(1);
                }

                arguments->set_netmask = true;
            } else {
                arguments->set_netmask = false;
                ipv4_addr = (char*)malloc(strlen(arg)+1);
                strcpy(ipv4_addr, arg);
            }

            break;
        case '6':
            if(arguments->noipv6)
            {
                perror("Sorry, but you had noipv6 set yet want to use ipv6?\n");
                exit(EXIT_FAILURE);
            }

            char* ipPart_s = strtok(arg, "/");
            char* prefixPart_s = strtok(NULL, "/");
            printf("ipPart_s: %s\n", ipPart_s);

            if(!prefixPart_s)
            {
                printf("No prefix length was provided\n");
                exit(1);
            }
            printf("prefixPart_s: %s\n", prefixPart_s);

            long prefixLen_l = strtol(prefixPart_s, NULL, 10); // TODO: Add handling here for errors (using errno)

						if(prefixLen_l == 0) {
								printf("Prefix length '%s' is not numeric\n", prefixPart_s);
								exit(EXIT_FAILURE);
						}

            arguments->ipv6 = ipPart_s;
            
            arguments->set_ipv6 = true;

            // Copy across global IPv6 address
            ipv6_addr = malloc(strlen(arguments->ipv6)+1);
            strcpy(ipv6_addr, arguments->ipv6);

            // Set global IPv6 prefix length
            ipv6_prefixLen = prefixLen_l;

            printf("MTU was %d, setting to minimum of %d as is required for IPv6\n", arguments->mtu, 1280);
            arguments->mtu = 1280;
            break;

        case 'l':
            if(arguments->noipv6)
            {
                perror("Sorry, but you had noipv6 set yet want to use ipv6 link-local?\n");
                exit(EXIT_FAILURE);
            }
            arguments->link_local_v6 = true;

            printf("MTU was %d, setting to minimum of %d as is required for IPv6\n", arguments->mtu, 1280);
            arguments->mtu = 1280;
            break;
						
        case 'n':
            arguments->noipv6 = true;
            if(arguments->set_ipv6)
            {
                printf("Requested no IPv6 yet you have set the IPv6 to '%s'\n", arguments->ipv6);
                exit(1);
            }
            break;

        case 'd':
            arguments->daemon = true;
            arguments->verbose = false;
            break;

        case 'T':
            arguments->kiss_over_tcp = true;
            break;

        case 'H':
            arguments->set_tcp_host = true;
            tcp_host = (char*)malloc(strlen(arg)+1);
            strcpy(tcp_host, arg);
            break;

        case 'P':
            arguments->set_tcp_port = true;
            tcp_port = atoi(arg);
            break;

        case 1:
            arguments->noup = true;
            break;

        case ARGP_KEY_ARG:
            // Check if there's now too many text arguments
            if (state->arg_num >= N_ARGS) argp_usage(state);

            // If not add to args
            arguments->args[state->arg_num] = arg;
            break;

        case ARGP_KEY_END:
            // Check if there's too few text arguments
            if (!arguments->kiss_over_tcp && state->arg_num < N_ARGS) argp_usage(state);

            // Check if text arguments were given when
            // KISS over TCP was specified
            if (arguments->kiss_over_tcp && state->arg_num != 0) argp_usage(state);

            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static void become_daemon() {
    pid_t pid;
    pid = fork();

    if (pid < 0) {
        perror("Fork failed");
        exit(EXIT_FAILURE);
    }

    if (pid > 0) {
        exit(0);
    }

    if (setsid() < 0) exit(1);

    signal(SIGCHLD, signal_handler);
    signal(SIGHUP, signal_handler);

    pid = fork();
    if (pid < 0) exit(1);
    if (pid > 0) exit(0);

    umask(0);
    chdir("/");

    openlog("tncattach", LOG_PID, LOG_DAEMON);
}

static struct argp argp = {options, parse_opt, args_doc, doc};
int main(int argc, char **argv) {
    struct arguments arguments;
    signal(SIGINT, signal_handler);

    arguments.baudrate = BAUDRATE_DEFAULT;
    arguments.mtu = MTU_DEFAULT;
    arguments.tap = false;
    arguments.verbose = false;
    arguments.set_ipv4 = false;
    arguments.set_netmask = false;
    arguments.set_ipv6 = false;
    arguments.link_local_v6 = false;
    arguments.set_netmask_v6 = false;
    arguments.noipv6 = false;
    arguments.daemon = false;
    arguments.noup = false;
    arguments.id_interval = -1;
    arguments.valid_id = false;
    arguments.kiss_over_tcp = false;

    argp_parse(&argp, argc, argv, 0, 0, &arguments);

    if (arguments.kiss_over_tcp) kiss_over_tcp = true;

    if (!kiss_over_tcp) {
        arguments.baudrate = atoi(arguments.args[1]);
    } else {
        if (!(arguments.set_tcp_host && arguments.set_tcp_port)) {
            if (!arguments.set_tcp_host) printf("Error: KISS over TCP was requested, but no host was specified\r\n");
            if (!arguments.set_tcp_port) printf("Error: KISS over TCP was requested, but no port was specified\r\n");
            exit(1);
        }
    }
    
    if (arguments.daemon) daemonize = true;
    if (arguments.verbose) verbose = true;
    if (arguments.tap) device_type = IF_TAP;
    if (arguments.noipv6) noipv6 = true;
    if (arguments.set_ipv4) set_ipv4 = true;
    if (arguments.set_netmask) set_netmask = true;
    if (arguments.set_ipv6) set_ipv6 = true;
    if (arguments.noup) noup = true;
    mtu = arguments.mtu;

    if (arguments.id_interval >= 0) {
        if (!arguments.valid_id) {
            printf("Error: Periodic identification requested, but no valid indentification data specified\r\n");
            cleanup();
            exit(1);
        } else {
            id_interval = arguments.id_interval;
            id = malloc(strlen(arguments.id));
            strcpy(id, arguments.id);
        }
    } else if (arguments.valid_id && arguments.id_interval == -1) {
        printf("Error: Periodic identification requested, but no indentification interval specified\r\n");
        cleanup();
        exit(1);
    }

    attached_if = open_tap();

    if (!arguments.kiss_over_tcp) {
        attached_tnc = open_port(arguments.args[0]);
        if (!setup_port(attached_tnc, arguments.baudrate)) {
            printf("Error during serial port setup");
            return 0;
        }
    } else {
        attached_tnc = open_tcp(tcp_host, tcp_port);
    }

    printf("TNC interface configured as %s\r\n", if_name);

    fds[IF_FD_INDEX].fd = attached_if;
    fds[IF_FD_INDEX].events = POLLIN;
    fds[TNC_FD_INDEX].fd = attached_tnc;
    fds[TNC_FD_INDEX].events = POLLIN;
    
    if (daemonize) {
        become_daemon();
        syslog(LOG_NOTICE, "tncattach daemon running");
    }

    read_loop();

    return 0;
}

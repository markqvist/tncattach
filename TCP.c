#include "TCP.h"

int open_tcp(char* ip, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        perror("Could not open AF_INET socket");
        exit(1);
    }

    struct hostent *server;
    struct sockaddr_in serv_addr;

    server = gethostbyname(ip);

    if (server == NULL) {
        perror("Error resolving host");
        exit(1);
    }

    memset(&serv_addr, 0, sizeof(serv_addr)); 
    serv_addr.sin_family = AF_INET;

    memcpy(server->h_addr, &serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(port);

    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Could not connect TCP socket");
        exit(1);
    }

    return sockfd;
}

int close_tcp(int fd) {
    return close(fd);
}
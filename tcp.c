#include "tcp.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

int listen_tcp(char const* node, char const* service) {
    struct addrinfo* res = NULL;
    struct addrinfo hints = {0, 0, SOCK_STREAM, 0, 0, 0, 0, 0};
    int err = getaddrinfo(node, service, &hints, &res);
    if (err != 0) {
        return err;
    }

    struct addrinfo* r = res;
    int fd = -1;
    for (; r != NULL; r = r->ai_next) {
        fd = socket(r->ai_family, r->ai_socktype|SOCK_CLOEXEC, r->ai_protocol);
        if (fd == -1) {
            continue;
        }

        int reuseaddr = 1;
        int err = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr));
        if (err != 0) {
            continue;
        }

        err = bind(fd, r->ai_addr, r->ai_addrlen);
        if (err != 0) {
            continue;
        }

        err = listen(fd, SOMAXCONN);
        if (err != 0) {
            continue;
        }

        break;
    }
    freeaddrinfo(res);

    if (r == NULL) {
        return EAI_SYSTEM;
    }

    return fd;
}

int dial_tcp(char const* node, char const* service) {
    struct addrinfo* res = NULL;
    struct addrinfo hints = {0, 0, SOCK_STREAM, 0, 0, 0, 0, 0};
    int err = getaddrinfo(node, service, &hints, &res);
    if (err != 0) {
        return err;
    }

    struct addrinfo const* r = res;
    int fd = -1;
    for (; r != NULL; r = r->ai_next) {
        fd = socket(r->ai_family, r->ai_socktype|SOCK_CLOEXEC, r->ai_protocol);
        if (fd == -1) {
            continue;
        }

        int err = connect(fd, r->ai_addr, r->ai_addrlen);
        if (err != 0) {
            continue;
        }

        break;
    }
    freeaddrinfo(res);

    if (r == NULL) {
        return EAI_SYSTEM;
    }

    return fd;
}

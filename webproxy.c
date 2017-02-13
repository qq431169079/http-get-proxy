#define _GNU_SOURCE
#include "tprintf.h"
#include "proxy.h"
#include "tcp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define LISTEN_ADDR "127.0.0.1"

int main(int argc, char const* const argv[]) {
    pthread_mutex_init(&stdout_mutex, NULL);

    if (argc < 2) {
        tprintf("usage: %s [port]\n", argv[0]);
        return 0;
    }
    char const* port = argv[1];

    int ln = listen_tcp(LISTEN_ADDR, port);
    if (ln < 0) {
        if (ln != EAI_SYSTEM) {
            tprintf("ListenTCP: %s\n", gai_strerror(ln));
        } else {
            perror("ListenTCP");
        }
        return 0;
    }

    while (1) {
        struct sockaddr_storage addr;
        socklen_t addrlen = sizeof(addr);
        int fd = accept4(ln, (struct sockaddr*)(&addr), &addrlen, SOCK_CLOEXEC);
        if (fd == -1) {
            perror("ln.accept4");
            continue;
        }

        {
            char buf[INET6_ADDRSTRLEN];
            char const* s = addr.ss_family == AF_INET ? inet_ntop(AF_INET, &(*(struct sockaddr_in*)(&addr)).sin_addr, buf, addrlen)
                                                      : inet_ntop(AF_INET6, &(*(struct sockaddr_in6*)(&addr)).sin6_addr, buf, addrlen);
            int port = addr.ss_family == AF_INET ? htons((*(struct sockaddr_in*)(&addr)).sin_port)
                                                 : htons((*(struct sockaddr_in6*)(&addr)).sin6_port);
            if (!s) {
                perror("inet_ntop");
            } else {
                tprintf("accepted new client connection from %s:%d\n", s, port);
            }
        }

        pthread_t thread;
        handle_client_args* args = malloc(sizeof(handle_client_args));
        args->client = fd;
        int err = pthread_create(&thread, NULL, handle_client, (void*)(args));
        if (err != 0) {
            tprintf("pthread_create: %s", strerror(err));
            close(args->client);
            free(args);
            continue;
        }

        err = pthread_detach(thread);
        if (err != 0) {
            tprintf("pthread_detach: %s", strerror(err));
            continue;
        }
    }

    close(ln);
}

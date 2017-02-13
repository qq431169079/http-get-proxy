#ifndef PROXY_H
#define PROXY_H
#include "tprintf.h"

typedef struct {
    int client;
} handle_client_args;

void* handle_client(void* ptr);

#endif

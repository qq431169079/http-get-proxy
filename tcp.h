#ifndef TCP_H
#define TCP_H
#include "tprintf.h"
#include <stdint.h>

int listen_tcp(char const* node, char const* service);
int dial_tcp(char const* node, char const* service);

#endif

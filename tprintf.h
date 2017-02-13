#ifndef TPRINTF_H
#define TPRINTF_H
#include <stdio.h>
#include <pthread.h>

extern pthread_mutex_t stdout_mutex;

// comment out this #define to allow
// ./webproxy to print debug info.
// #define CSCI_TEST_PY

#ifdef CSCI_TEST_PY
#define tprintf(...)
#else
#define tprintf(...) do {                   \
    pthread_mutex_lock(&stdout_mutex);      \
    printf(__VA_ARGS__);                    \
    pthread_mutex_unlock(&stdout_mutex);    \
} while (0)
#endif

#endif
